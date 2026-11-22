#include "worker_session.hpp"

#include "../common/protocol.hpp"
#include "output_files.hpp"
#include "session_input.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace cinemania::worker
{
namespace
{
constexpr std::uint32_t kMaxVideoFilterThreads = 4;
constexpr wchar_t kAudioBitrate[] = L"512k";
constexpr wchar_t kVideoFilter[] =
    L"vflip,scale=in_range=full:out_range=limited:out_color_matrix=bt709,format=yuv420p,"
    L"setparams=range=limited:color_primaries=bt709:color_trc=bt709:colorspace=bt709";

Deadline deadline()
{
    return std::chrono::steady_clock::now() + std::chrono::seconds(30);
}

NativeError failure(DWORD code, const char* message)
{
    return {code, message};
}

enum class EncoderMode
{
    automatic,
    cpu,
    gpu
};

// CINEMANIA_ENCODER=cpu forces x264, =gpu forces NVENC (it fails if it's unavailable),
// anything else (or unset) tries NVENC first and falls back to x264
EncoderMode requested_encoder()
{
    wchar_t value[16]{};
    const DWORD length = GetEnvironmentVariableW(L"CINEMANIA_ENCODER", value, 16);
    if (length == 0 || length >= 16)
    {
        return EncoderMode::automatic;
    }
    const std::wstring text(value, length);
    if (_wcsicmp(text.c_str(), L"cpu") == 0)
    {
        return EncoderMode::cpu;
    }
    if (_wcsicmp(text.c_str(), L"gpu") == 0 || _wcsicmp(text.c_str(), L"nvenc") == 0)
    {
        return EncoderMode::gpu;
    }
    return EncoderMode::automatic;
}

std::vector<std::wstring> preflight_arguments(const std::wstring& path,
                                              bool audio_expected,
                                              bool nvenc)
{
    std::vector<std::wstring> arguments = {L"-hide_banner",
                                           L"-loglevel",
                                           L"warning",
                                           L"-nostdin",
                                           L"-filter_threads",
                                           L"1",
                                           L"-y",
                                           L"-f",
                                           L"lavfi",
                                           L"-i",
                                           // NVENC rejects frames below its hardware minimum size
                                           nvenc ? L"color=size=256x256:rate=1"
                                                 : L"color=size=2x2:rate=1"};
    if (audio_expected)
    {
        arguments.insert(arguments.end(), {L"-f", L"lavfi", L"-i", L"anullsrc=r=48000:cl=stereo"});
    }
    arguments.insert(arguments.end(), {L"-frames:v", L"1", L"-t", L"1"});
    if (nvenc)
    {
        arguments.insert(arguments.end(),
                         {L"-c:v", L"h264_nvenc", L"-preset", L"p5", L"-pix_fmt", L"yuv420p"});
    }
    else
    {
        arguments.insert(arguments.end(),
                         {L"-c:v",
                          L"libx264",
                          L"-threads:v",
                          L"1",
                          L"-preset",
                          L"veryfast",
                          L"-crf",
                          L"18",
                          L"-pix_fmt",
                          L"yuv420p"});
    }
    if (audio_expected)
    {
        arguments.insert(arguments.end(),
                         {L"-c:a", L"aac", L"-threads:a", L"1", L"-b:a", kAudioBitrate});
    }
    arguments.insert(arguments.end(), {L"-progress", L"pipe:2", L"-f", L"mp4", path});
    return arguments;
}

std::vector<std::wstring>
video_arguments(const SessionFormat& format, const std::wstring& path, bool nvenc)
{
    // CRF 0 is lossless for x264 but "automatic" for NVENC, so keep NVENC at 1 or above
    const std::vector<std::wstring> encoder =
        nvenc ? std::vector<std::wstring>{L"-c:v",
                                          L"h264_nvenc",
                                          L"-preset",
                                          L"p5",
                                          L"-tune",
                                          L"hq",
                                          L"-rc",
                                          L"vbr",
                                          L"-cq",
                                          std::to_wstring((std::max)(format.crf, 1u)),
                                          L"-b:v",
                                          L"0",
                                          L"-profile:v",
                                          L"high"}
              : std::vector<std::wstring>{L"-c:v",
                                          L"libx264",
                                          L"-preset",
                                          L"veryfast",
                                          L"-crf",
                                          std::to_wstring(format.crf),
                                          L"-threads",
                                          std::to_wstring(format.threads)};
    std::vector<std::wstring> arguments = {L"-hide_banner",
            L"-loglevel",
            L"warning",
            L"-nostdin",
            L"-filter_threads",
            std::to_wstring((std::min)(format.threads, kMaxVideoFilterThreads)),
            L"-y",
            L"-f",
            L"rawvideo",
            L"-pixel_format",
            L"bgr24",
            L"-video_size",
            std::to_wstring(format.width) + L"x" + std::to_wstring(format.height),
            L"-framerate",
            std::to_wstring(format.fps_numerator) + L"/" + std::to_wstring(format.fps_denominator),
            L"-i",
            L"pipe:0",
            L"-vf",
            kVideoFilter};
    arguments.insert(arguments.end(), encoder.begin(), encoder.end());
    arguments.insert(arguments.end(),
           {L"-color_range",
            L"tv",
            L"-colorspace",
            L"bt709",
            L"-color_primaries",
            L"bt709",
            L"-color_trc",
            L"bt709",
            L"-progress",
            L"pipe:2",
            L"-f",
            L"mp4",
            path});
    return arguments;
}

std::vector<std::wstring> mux_arguments(const SessionFormat& format,
                                        const std::wstring& video,
                                        const std::wstring& audio,
                                        const std::wstring& candidate)
{
    std::vector<std::wstring> arguments = {L"-hide_banner",
                                           L"-loglevel",
                                           L"warning",
                                           L"-nostdin",
                                           L"-filter_threads",
                                           L"1",
                                           L"-y",
                                           L"-i",
                                           video};
    if (format.audio_expected)
    {
        const std::vector<std::wstring> input = {
            L"-f",         L"s16le",
            L"-ar",        std::to_wstring(format.sample_rate),
            L"-ac",        std::to_wstring(format.channels),
            L"-i",         audio,
            L"-map",       L"0:v:0",
            L"-map",       L"1:a:0",
            L"-c:v",       L"copy",
            L"-c:a",       L"aac",
            L"-threads:a", L"1",
            L"-ar",        std::to_wstring(format.sample_rate),
            L"-b:a",       kAudioBitrate,
            L"-af",        L"asetpts=PTS+" + std::to_wstring(format.audio_offset) + L"/SR/TB"};
        arguments.insert(arguments.end(), input.begin(), input.end());
    }
    else
    {
        arguments.insert(arguments.end(), {L"-map", L"0:v:0", L"-c:v", L"copy"});
    }
    arguments.insert(
        arguments.end(),
        {L"-movflags", L"+faststart", L"-progress", L"pipe:2", L"-f", L"mp4", candidate});
    return arguments;
}

NativeError run_finished_process(const std::wstring& executable,
                                 const std::vector<std::wstring>& arguments,
                                 StopState& stop,
                                 const std::function<NativeError()>& progress = {})
{
    EncoderProcess process;
    auto error = process.start(executable, arguments, stop);
    if (error)
    {
        return error;
    }
    process.close_stdin();
    const auto result = process.finish(stop, deadline(), progress);
    if (result.error)
    {
        return result.error;
    }
    if (result.exit_code != 0)
    {
        return {result.exit_code, result.stderr_tail};
    }
    return {};
}

void control_loop(PipeChannel& channel, StopState& stop, std::uint64_t session)
{
    std::array<std::byte, protocol::kHeaderBytes> bytes{};
    const auto error = channel.read_exact(bytes, stop, Deadline::max());
    if (error)
    {
        if (!stop.stopped())
        {
            stop.request(error, StopReason::failure, StopOrigin::control);
        }
        return;
    }
    protocol::Header header{};
    if (!protocol::decode_header(bytes, header) || header.session != session ||
        header.sequence != 0 || header.type != protocol::MessageType::cancel ||
        header.payload_bytes != 0)
    {
        stop.request(failure(ERROR_INVALID_DATA, "invalid control message"),
                     StopReason::failure,
                     StopOrigin::control);
        return;
    }
    stop.request(failure(ERROR_CANCELLED, "cancelled"), StopReason::cancelled, StopOrigin::control);
}

class ControlJoin
{
public:
    ControlJoin(StopState& stop, std::thread& thread) : stop_(stop), thread_(thread)
    {
    }

    ~ControlJoin()
    {
        join();
    }

    void join()
    {
        if (thread_.joinable())
        {
            stop_.wake();
            thread_.join();
        }
    }

private:
    StopState& stop_;
    std::thread& thread_;
};

class Session
{
public:
    Session(const WorkerOptions& options, StopState& stop, PipeChannel& status)
        : options_(options), stop_(stop), status_(status), input_(options.session)
    {
    }

    [[nodiscard]] NativeError run(PipeChannel& data);
    [[nodiscard]] NativeError
    send_result(const NativeError& error, bool cancelled, StopOrigin origin);
    [[nodiscard]] NativeError send_progress();
    [[nodiscard]] NativeError cleanup();

private:
    [[nodiscard]] NativeError
    read_message(PipeChannel& channel, protocol::Header& header, std::vector<std::byte>& payload);
    [[nodiscard]] NativeError handle_message(const protocol::Header& header,
                                             std::span<const std::byte> payload);
    [[nodiscard]] NativeError begin();
    [[nodiscard]] NativeError finish();
    [[nodiscard]] NativeError send_status(protocol::MessageType type,
                                          std::span<const std::byte> payload,
                                          StopState& stop,
                                          Deadline write_deadline);

    const WorkerOptions& options_;
    StopState& stop_;
    PipeChannel& status_;
    SessionInput input_;
    OutputFiles output_;
    EncoderProcess video_;
    OwnedHandle audio_;
    std::wstring video_path_;
    std::wstring audio_path_;
    std::uint64_t audio_bytes_ = 0;
    std::uint64_t accepted_frames_ = 0;
    std::uint64_t accepted_samples_ = 0;
    std::uint64_t status_sequence_ = 0;
    protocol::Stage stage_ = protocol::Stage::protocol;
    bool video_input_open_ = false;
    bool use_nvenc_ = false;
};

NativeError Session::send_status(protocol::MessageType type,
                                 std::span<const std::byte> payload,
                                 StopState& stop,
                                 Deadline write_deadline)
{
    auto header = protocol::encode_header(
        {type, options_.session, status_sequence_, static_cast<std::uint32_t>(payload.size())});
    header.insert(header.end(), payload.begin(), payload.end());
    auto error = status_.write_all(header, stop, write_deadline);
    if (!error)
    {
        ++status_sequence_;
    }
    return error;
}

NativeError Session::send_progress()
{
    std::vector<std::byte> payload;
    protocol::append_u64(payload, accepted_frames_);
    protocol::append_u64(payload, accepted_samples_);
    return send_status(protocol::MessageType::progress, payload, stop_, deadline());
}

NativeError Session::read_message(PipeChannel& channel,
                                  protocol::Header& header,
                                  std::vector<std::byte>& payload)
{
    std::array<std::byte, protocol::kHeaderBytes> bytes{};
    auto error = channel.read_exact(
        bytes, stop_, deadline(), video_input_open_ ? video_.process_handle() : nullptr);
    if (error)
    {
        return error;
    }
    if (!protocol::decode_header(bytes, header) || !input_.validate_header(header))
    {
        stage_ = protocol::Stage::protocol;
        return failure(ERROR_INVALID_DATA, "invalid data header");
    }
    payload.resize(header.payload_bytes);
    error = channel.read_exact(
        payload, stop_, deadline(), video_input_open_ ? video_.process_handle() : nullptr);
    if (error)
    {
        return error;
    }
    if (!input_.accept(header, payload))
    {
        stage_ = protocol::Stage::protocol;
        return failure(ERROR_INVALID_DATA, "invalid data payload or order");
    }
    return {};
}

NativeError Session::begin()
{
    stage_ = protocol::Stage::preflight;
    DWORD code = output_.prepare(input_.format().destination, options_.scratch);
    if (code != ERROR_SUCCESS)
    {
        return failure(code, "cannot reserve destination");
    }
    std::wstring preflight_path;
    code = output_.make_temporary(preflight_path);
    if (code != ERROR_SUCCESS)
    {
        return failure(code, "cannot create preflight file");
    }
    const EncoderMode mode = requested_encoder();
    use_nvenc_ = false;
    NativeError error;
    if (mode != EncoderMode::cpu)
    {
        error = run_finished_process(
            options_.ffmpeg,
            preflight_arguments(preflight_path, input_.format().audio_expected, true),
            stop_);
        if (!error)
        {
            use_nvenc_ = true;
        }
        else if (mode == EncoderMode::gpu || stop_.stopped())
        {
            return error; // GPU was explicitly requested, so report why it failed
        }
    }
    if (!use_nvenc_)
    {
        error = run_finished_process(
            options_.ffmpeg,
            preflight_arguments(preflight_path, input_.format().audio_expected, false),
            stop_);
        if (error)
        {
            return error;
        }
    }
    code = output_.make_temporary(video_path_);
    if (code != ERROR_SUCCESS)
    {
        return failure(code, "cannot create video file");
    }
    error = video_.start(options_.ffmpeg, video_arguments(input_.format(), video_path_, use_nvenc_), stop_);
    if (error)
    {
        return error;
    }
    video_input_open_ = true;
    error = send_status(protocol::MessageType::ready, {}, stop_, deadline());
    if (!error)
    {
        stage_ = protocol::Stage::video;
    }
    return error;
}

NativeError Session::handle_message(const protocol::Header& header,
                                    std::span<const std::byte> payload)
{
    switch (header.type)
    {
        case protocol::MessageType::begin_session:
            return begin();
        case protocol::MessageType::video_frame:
        {
            stage_ = protocol::Stage::video;
            auto error = video_.write(payload.subspan(8), stop_, deadline());
            if (!error)
            {
                ++accepted_frames_;
            }
            return error;
        }
        case protocol::MessageType::end_video:
            stage_ = protocol::Stage::video;
            video_.close_stdin();
            video_input_open_ = false;
            return {};
        case protocol::MessageType::audio_format:
        {
            stage_ = protocol::Stage::audio;
            const DWORD code = output_.make_temporary(audio_path_);
            if (code != ERROR_SUCCESS)
            {
                return failure(code, "cannot create audio spool");
            }
            audio_.reset(CreateFileW(audio_path_.c_str(),
                                     GENERIC_WRITE,
                                     0,
                                     nullptr,
                                     OPEN_EXISTING,
                                     FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_OVERLAPPED,
                                     nullptr));
            if (audio_.get() == INVALID_HANDLE_VALUE)
            {
                return failure(GetLastError(), "cannot open audio spool");
            }
            return {};
        }
        case protocol::MessageType::audio_samples:
        {
            stage_ = protocol::Stage::audio;
            const auto samples = payload.subspan(12);
            auto error = file_write_all(audio_.get(), samples, audio_bytes_, stop_, deadline());
            if (!error)
            {
                audio_bytes_ += samples.size();
                accepted_samples_ += samples.size() / input_.format().block_alignment;
            }
            return error;
        }
        case protocol::MessageType::end_audio:
            audio_.reset();
            return {};
        case protocol::MessageType::finish:
            return finish();
        default:
            return failure(ERROR_INVALID_DATA, "unexpected message");
    }
}

NativeError Session::finish()
{
    stage_ = protocol::Stage::video;
    const auto progress = [this]()
    {
        return send_progress();
    };
    const auto video_result = video_.finish(stop_, deadline(), progress);
    if (video_result.error)
    {
        return video_result.error;
    }
    if (video_result.exit_code != 0)
    {
        return {video_result.exit_code, video_result.stderr_tail};
    }
    if (!video_result.frame_count || *video_result.frame_count != accepted_frames_)
    {
        return failure(ERROR_INVALID_DATA, "encoded frame count mismatch");
    }
    stage_ = protocol::Stage::mux;
    std::wstring candidate;
    DWORD code = output_.make_temporary(candidate);
    if (code != ERROR_SUCCESS)
    {
        return failure(code, "cannot create final candidate");
    }
    auto error =
        run_finished_process(options_.ffmpeg,
                             mux_arguments(input_.format(), video_path_, audio_path_, candidate),
                             stop_,
                             progress);
    if (error)
    {
        return error;
    }
    stage_ = protocol::Stage::publish;
    error = send_progress();
    if (error)
    {
        return error;
    }
    return stop_.commit_if_running(
        [this, &candidate]()
        {
            const DWORD publish_code = output_.publish(candidate);
            if (publish_code != ERROR_SUCCESS)
            {
                return failure(publish_code, "cannot publish destination");
            }
            return NativeError{};
        });
}

NativeError Session::run(PipeChannel& data)
{
    std::vector<std::byte> payload;
    for (;;)
    {
        protocol::Header header{};
        auto error = read_message(data, header, payload);
        if (error)
        {
            return error;
        }
        error = handle_message(header, payload);
        if (error)
        {
            return error;
        }
        if (header.type == protocol::MessageType::finish)
        {
            return {};
        }
        if (header.type == protocol::MessageType::video_frame ||
            header.type == protocol::MessageType::audio_samples ||
            header.type == protocol::MessageType::end_video ||
            header.type == protocol::MessageType::end_audio)
        {
            error = send_progress();
            if (error)
            {
                return error;
            }
        }
    }
}

NativeError Session::cleanup()
{
    audio_.reset();
    const auto process_error = video_.close();
    const DWORD code = output_.cleanup();
    if (process_error)
    {
        return process_error;
    }
    if (code != ERROR_SUCCESS)
    {
        return failure(code, "failed to remove owned temporary files");
    }
    return {};
}

NativeError Session::send_result(const NativeError& error, bool cancelled, StopOrigin origin)
{
    std::vector<std::byte> payload;
    protocol::Outcome outcome = protocol::Outcome::success;
    if (error)
    {
        outcome = cancelled ? protocol::Outcome::cancelled : protocol::Outcome::failure;
    }
    auto stage = stage_;
    if (origin == StopOrigin::control)
    {
        stage = protocol::Stage::control;
    }
    else if (origin == StopOrigin::encoder && stage == protocol::Stage::audio)
    {
        stage = protocol::Stage::video;
    }
    protocol::append_u32(payload, static_cast<std::uint32_t>(outcome));
    protocol::append_u32(payload, static_cast<std::uint32_t>(stage));
    protocol::append_u32(payload, error.code);
    protocol::append_u64(payload, accepted_frames_);
    protocol::append_u64(payload, accepted_samples_);
    const std::string message = error.message.substr(0, 4096);
    protocol::append_u32(payload, static_cast<std::uint32_t>(message.size()));
    for (const unsigned char character : message)
    {
        const unsigned char safe_character = character == 0 || character >= 0x80 ? '?' : character;
        payload.push_back(static_cast<std::byte>(safe_character));
    }
    StopState terminal_stop;
    return send_status(protocol::MessageType::result,
                       payload,
                       terminal_stop,
                       std::chrono::steady_clock::now() + std::chrono::seconds(2));
}
} // namespace

int run_session(const WorkerOptions& options)
{
    StopState stop;
    PipeChannel data(options.data);
    PipeChannel control(options.control);
    PipeChannel status(options.status);
    Session session(options, stop, status);
    if (!stop.event())
    {
        return 1;
    }
    std::thread control_thread(
        [&control, &stop, &options]()
        {
            try
            {
                control_loop(control, stop, options.session);
            }
            catch (...)
            {
                stop.request(failure(ERROR_NOT_ENOUGH_MEMORY, "control thread failed"),
                             StopReason::failure,
                             StopOrigin::control);
            }
        });
    ControlJoin control_join(stop, control_thread);
    NativeError error;
    try
    {
        error = session.run(data);
    }
    catch (...)
    {
        error = failure(ERROR_NOT_ENOUGH_MEMORY, "session failed unexpectedly");
    }
    if (error)
    {
        stop.request(error);
        error = stop.error();
    }
    control_join.join();
    const bool cancelled = error && stop.stopped() && stop.reason() == StopReason::cancelled;
    const StopOrigin origin = stop.stopped() ? stop.origin() : StopOrigin::operation;
    const auto cleanup_error = session.cleanup();
    if (cleanup_error)
    {
        if (error)
        {
            if (error.message.size() > 3072)
            {
                error.message.resize(3072);
            }
            error.message += "; cleanup: " + cleanup_error.message + " (code " +
                             std::to_string(cleanup_error.code) + ")";
        }
        else
        {
            error = cleanup_error;
        }
    }
    const auto status_error = session.send_result(error, cancelled, origin);
    if (status_error)
    {
        return 1;
    }
    if (!error)
    {
        return 0;
    }
    return cancelled ? 2 : 1;
}
} // namespace cinemania::worker
