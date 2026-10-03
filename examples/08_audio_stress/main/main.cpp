#include "AudioEngine.hpp"
#include "BoardPins.hpp"
#include "sd_card.hpp"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h" // IWYU pragma: keep
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "soc/soc_caps.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <sys/stat.h>
#include <vector>

namespace {

using Espressif::Wrappers::SdCard;
using Espressif::Wrappers::Audio::AudioEngine;
using Espressif::Wrappers::Audio::ChannelId;
using Espressif::Wrappers::Audio::INVALID_CHANNEL;

constexpr const char* TAG = "AUDIO_STRESS";

#if SOC_SDMMC_HOST_SUPPORTED
constexpr bool kSdmmcSupported = true;
#else
constexpr bool kSdmmcSupported = false;
#endif

#if CONFIG_STRESS_SD_CONTENTION
constexpr bool kSdContention = true;
#else
constexpr bool kSdContention = false;
#endif

#if CONFIG_STRESS_START_MODE_SEQUENTIAL
constexpr const char* kStartModeName = "sequential";
#endif

constexpr const char* kMountPoint = "/sdcard";
constexpr const char* kAssetDir = "/sdcard/stress";
constexpr const char* kLoopAPath = "/sdcard/stress/loop_a.wav";
constexpr const char* kLoopBPath = "/sdcard/stress/loop_b.wav";
constexpr const char* kScratchPath = "/sdcard/stress/scratch.bin";

constexpr uint32_t kSampleRate = 44100;
constexpr int16_t kNoisePeak = 24000;
constexpr uint32_t kLoopDurationMs = 250;
constexpr uint32_t kLoopSeed = 0x1234ABCDU;

struct ShotAsset {
    const char* path;
    uint32_t duration_ms;
    uint32_t seed;
};

constexpr std::array<ShotAsset, 3> kShots = {{
    {"/sdcard/stress/shot_1.wav", 40, 0x0BADF00DU},
    {"/sdcard/stress/shot_2.wav", 150, 0x5EED5EEDU},
    {"/sdcard/stress/shot_3.wav", 400, 0xC0FFEE11U},
}};

constexpr uint32_t kLongestShotMs =
    std::max_element(kShots.begin(), kShots.end(), [](const ShotAsset& a, const ShotAsset& b) {
        return a.duration_ms < b.duration_ms;
    })->duration_ms;

constexpr size_t kScratchBytes = 2 * 1024 * 1024;
constexpr size_t kSdChunkBytes = 32 * 1024;

constexpr uint32_t kQuiesceSettleMs = 200;
constexpr uint32_t kTaskStackBytes = 4096;
constexpr TickType_t kPollTicks = 1;
constexpr EventBits_t kStopBit = BIT0;

constexpr BaseType_t kControllerCore = (portNUM_PROCESSORS > 1) ? 1 : tskNO_AFFINITY;
constexpr UBaseType_t kPairPriority = 7;
constexpr UBaseType_t kShotPriority = 5;
constexpr UBaseType_t kStormPriority = 7;
constexpr UBaseType_t kContentionPriority = 2;
constexpr UBaseType_t kReportPriority = 1;

struct FileCloser {
    void operator()(FILE* file) const { std::fclose(file); }
};
using FileHandle = std::unique_ptr<FILE, FileCloser>;

struct [[gnu::packed]] WavHeader {
    char riff_id[4] = {'R', 'I', 'F', 'F'};
    uint32_t riff_size = 0;
    char wave_id[4] = {'W', 'A', 'V', 'E'};
    char fmt_id[4] = {'f', 'm', 't', ' '};
    uint32_t fmt_size = 16;
    uint16_t audio_format = 1;
    uint16_t num_channels = 1;
    uint32_t sample_rate = kSampleRate;
    uint32_t byte_rate = kSampleRate * sizeof(int16_t);
    uint16_t block_align = sizeof(int16_t);
    uint16_t bits_per_sample = 16;
    char data_id[4] = {'d', 'a', 't', 'a'};
    uint32_t data_size = 0;
};
static_assert(sizeof(WavHeader) == 44);

class LfsrNoise {
public:
    explicit LfsrNoise(uint32_t seed) : _state(seed != 0 ? seed : 1) {}

    int16_t next() {
        _state ^= _state << 13;
        _state ^= _state >> 17;
        _state ^= _state << 5;
        const auto raw = static_cast<int16_t>(_state >> 16);
        return std::clamp<int16_t>(raw, -kNoisePeak, kNoisePeak);
    }

private:
    uint32_t _state;
};

std::vector<int16_t> generateNoise(uint32_t seed, uint32_t duration_ms) {
    LfsrNoise noise(seed);
    std::vector<int16_t> samples(kSampleRate * duration_ms / 1000);
    std::generate(samples.begin(), samples.end(), [&noise] { return noise.next(); });
    return samples;
}

uint32_t randomBetween(uint32_t low, uint32_t high) {
    const auto [lo, hi] = std::minmax(low, high);
    return lo + (esp_random() % (hi - lo + 1));
}

esp_err_t ensureDirectory(const char* path) {
    struct stat info {};
    if (stat(path, &info) == 0) {
        return S_ISDIR(info.st_mode) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }
    return (mkdir(path, 0775) == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t closeChecked(FileHandle& file) {
    return (std::fclose(file.release()) == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t writeWav(const char* path, std::span<const int16_t> samples) {
    FileHandle file{std::fopen(path, "wb")};
    if (!file) {
        ESP_LOGE(TAG, "Cannot create %s", path);
        return ESP_FAIL;
    }

    WavHeader header;
    header.data_size = static_cast<uint32_t>(samples.size_bytes());
    header.riff_size = header.data_size + sizeof(WavHeader) - 8;

    if (std::fwrite(&header, sizeof(header), 1, file.get()) != 1 ||
        std::fwrite(samples.data(), sizeof(int16_t), samples.size(), file.get()) != samples.size()) {
        ESP_LOGE(TAG, "Short write on %s", path);
        return ESP_FAIL;
    }
    return closeChecked(file);
}

esp_err_t writeScratchFile() {
    struct stat info {};
    if (stat(kScratchPath, &info) == 0 && static_cast<size_t>(info.st_size) == kScratchBytes) {
        return ESP_OK;
    }

    FileHandle file{std::fopen(kScratchPath, "wb")};
    if (!file) {
        ESP_LOGE(TAG, "Cannot create %s", kScratchPath);
        return ESP_FAIL;
    }

    const std::vector<uint8_t> chunk(kSdChunkBytes, 0xA5);
    for (size_t written = 0; written < kScratchBytes; written += chunk.size()) {
        if (std::fwrite(chunk.data(), 1, chunk.size(), file.get()) != chunk.size()) {
            ESP_LOGE(TAG, "Short write on %s", kScratchPath);
            return ESP_FAIL;
        }
    }
    return closeChecked(file);
}

esp_err_t generateAssets() {
    if (const esp_err_t err = ensureDirectory(kAssetDir); err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot create %s: %s", kAssetDir, esp_err_to_name(err));
        return err;
    }

    std::vector<int16_t> loop = generateNoise(kLoopSeed, kLoopDurationMs);
    if (const esp_err_t err = writeWav(kLoopAPath, loop); err != ESP_OK) {
        return err;
    }
    std::transform(loop.begin(), loop.end(), loop.begin(), [](int16_t s) { return static_cast<int16_t>(-s); });
    if (const esp_err_t err = writeWav(kLoopBPath, loop); err != ESP_OK) {
        return err;
    }

    for (const ShotAsset& shot : kShots) {
        if (const esp_err_t err = writeWav(shot.path, generateNoise(shot.seed, shot.duration_ms)); err != ESP_OK) {
            return err;
        }
    }

    if (kSdContention) {
        return writeScratchFile();
    }
    return ESP_OK;
}

struct StressContext {
    explicit StressContext(AudioEngine& audio_engine)
        : engine(audio_engine),
          stop_event(xEventGroupCreateStatic(&stop_event_storage)),
          start_us(esp_timer_get_time()) {}

    ~StressContext() { vEventGroupDelete(stop_event); }

    StressContext(const StressContext&) = delete;
    StressContext& operator=(const StressContext&) = delete;

    AudioEngine& engine;
    StaticEventGroup_t stop_event_storage{};
    EventGroupHandle_t stop_event;
    const int64_t start_us;

    std::atomic<bool> pair_loading{false};
    std::atomic<ChannelId> last_pair_id{INVALID_CHANNEL};

    std::atomic<uint32_t> loops_started{0};
    std::atomic<uint32_t> loops_stopped{0};
    std::atomic<uint32_t> shots_played{0};
    std::atomic<uint32_t> shots_stopped{0};
    std::atomic<uint32_t> storm_stops{0};
    std::atomic<uint32_t> play_failures{0};
    std::atomic<uint32_t> contention_kib{0};
};

bool isStopping(const StressContext& ctx) {
    return (xEventGroupGetBits(ctx.stop_event) & kStopBit) != 0;
}

bool sleepUnlessStopping(const StressContext& ctx, uint32_t ms) {
    const EventBits_t bits = xEventGroupWaitBits(ctx.stop_event, kStopBit, pdFALSE, pdTRUE, pdMS_TO_TICKS(ms));
    return (bits & kStopBit) == 0;
}

ChannelId playCounted(StressContext& ctx, const char* path, bool loop, uint16_t volume,
                      std::atomic<uint32_t>& started) {
    const ChannelId id = ctx.engine.play(path, loop, volume);
    if (id == INVALID_CHANNEL) {
        ctx.play_failures.fetch_add(1, std::memory_order_relaxed);
    } else {
        started.fetch_add(1, std::memory_order_relaxed);
    }
    return id;
}

void stopLayer(StressContext& ctx, ChannelId& id) {
    if (id == INVALID_CHANNEL) {
        return;
    }
    ctx.engine.stop(id);
    ctx.loops_stopped.fetch_add(1, std::memory_order_relaxed);
    id = INVALID_CHANNEL;
}

ChannelId playLayer(StressContext& ctx, const char* path) {
    const ChannelId id = playCounted(ctx, path, true, CONFIG_STRESS_LOOP_VOLUME, ctx.loops_started);
    if (id != INVALID_CHANNEL) {
        ctx.last_pair_id.store(id);
    }
    return id;
}

void startPair(StressContext& ctx, ChannelId& layer_a, ChannelId& layer_b) {
    ctx.pair_loading.store(true);
    layer_a = playLayer(ctx, kLoopAPath);
    layer_b = playLayer(ctx, kLoopBPath);
    ctx.pair_loading.store(false);
}

void pairControllerBody(StressContext& ctx) {
    ChannelId layer_a = INVALID_CHANNEL;
    ChannelId layer_b = INVALID_CHANNEL;
    do {
        stopLayer(ctx, layer_a);
        stopLayer(ctx, layer_b);
        startPair(ctx, layer_a, layer_b);
    } while (sleepUnlessStopping(ctx, randomBetween(CONFIG_STRESS_PAIR_WAIT_MIN_MS, CONFIG_STRESS_PAIR_WAIT_MAX_MS)));
    stopLayer(ctx, layer_a);
    stopLayer(ctx, layer_b);
}

void shotControllerBody(StressContext& ctx) {
    do {
        const ShotAsset& shot = kShots[randomBetween(0, kShots.size() - 1)];
        const ChannelId id = playCounted(ctx, shot.path, false, CONFIG_STRESS_SHOT_VOLUME, ctx.shots_played);
        if (id != INVALID_CHANNEL && randomBetween(1, CONFIG_STRESS_SHOT_STOP_ONE_IN) == 1) {
            static_cast<void>(sleepUnlessStopping(ctx, randomBetween(0, CONFIG_STRESS_SHOT_STOP_DELAY_MAX_MS)));
            ctx.engine.stop(id);
            ctx.shots_stopped.fetch_add(1, std::memory_order_relaxed);
        }
    } while (
        sleepUnlessStopping(ctx, randomBetween(CONFIG_STRESS_SHOT_INTERVAL_MIN_MS, CONFIG_STRESS_SHOT_INTERVAL_MAX_MS)));
}

void stopStormBody(StressContext& ctx) {
    constexpr int64_t kPeriodUs = static_cast<int64_t>(CONFIG_STRESS_STORM_PERIOD_S) * 1000000;
    int64_t idle_ms = 0;
    do {
        const int64_t storm_start_us = esp_timer_get_time();
        uint32_t stops = 0;
        while (stops < CONFIG_STRESS_STORM_STOPS && !isStopping(ctx)) {
            if (ctx.pair_loading.load()) {
                ctx.engine.stop(ctx.last_pair_id.load());
                ctx.storm_stops.fetch_add(1, std::memory_order_relaxed);
                ++stops;
            }
            vTaskDelay(kPollTicks);
        }
        const int64_t elapsed_us = esp_timer_get_time() - storm_start_us;
        idle_ms = std::max<int64_t>(kPeriodUs - elapsed_us, 0) / 1000;
    } while (sleepUnlessStopping(ctx, static_cast<uint32_t>(idle_ms)));
}

void sdContentionBody(StressContext& ctx) {
    std::vector<uint8_t> chunk(kSdChunkBytes);
    while (!isStopping(ctx)) {
        FileHandle file{std::fopen(kScratchPath, "rb")};
        if (!file) {
            ESP_LOGE(TAG, "Cannot open %s", kScratchPath);
            static_cast<void>(sleepUnlessStopping(ctx, 1000));
            continue;
        }
        size_t bytes_read = std::fread(chunk.data(), 1, chunk.size(), file.get());
        while (bytes_read > 0 && !isStopping(ctx)) {
            ctx.contention_kib.fetch_add(static_cast<uint32_t>(bytes_read / 1024), std::memory_order_relaxed);
            vTaskDelay(kPollTicks);
            bytes_read = std::fread(chunk.data(), 1, chunk.size(), file.get());
        }
    }
}

int stackFreeBytes(const char* task_name) {
    const TaskHandle_t task = xTaskGetHandle(task_name);
    return (task != nullptr) ? static_cast<int>(uxTaskGetStackHighWaterMark(task)) : -1;
}

void logReport(const StressContext& ctx) {
    const auto elapsed_s = static_cast<unsigned long>((esp_timer_get_time() - ctx.start_us) / 1000000);
    ESP_LOGI(TAG,
             "t=%lus loops +%" PRIu32 "/-%" PRIu32 " shots %" PRIu32 " (stopped %" PRIu32 ") storm %" PRIu32
             " play_fail %" PRIu32 " sd %" PRIu32 " KiB | heap int %u psram %u | stack free mixer %d sd %d mem %d",
             elapsed_s, ctx.loops_started.load(), ctx.loops_stopped.load(), ctx.shots_played.load(),
             ctx.shots_stopped.load(), ctx.storm_stops.load(), ctx.play_failures.load(), ctx.contention_kib.load(),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)), stackFreeBytes("audio_mixer"),
             stackFreeBytes("audio_sd_reader"), stackFreeBytes("audio_mem_reader"));
}

void reportBody(StressContext& ctx) {
    while (sleepUnlessStopping(ctx, CONFIG_STRESS_REPORT_PERIOD_S * 1000)) {
        logReport(ctx);
    }
}

class WorkerTask {
public:
    using Body = void (*)(StressContext&);

    WorkerTask(const char* name, Body body, StressContext& ctx, UBaseType_t priority, BaseType_t core)
        : _body(body), _ctx(ctx), _done(xSemaphoreCreateBinaryStatic(&_done_storage)) {
        if (xTaskCreatePinnedToCore(&WorkerTask::entry, name, kTaskStackBytes, this, priority, &_handle, core) !=
            pdPASS) {
            ESP_LOGE(TAG, "Cannot create task %s", name);
            _handle = nullptr;
        }
    }

    ~WorkerTask() {
        if (_handle != nullptr) {
            xSemaphoreTake(_done, portMAX_DELAY);
        }
        vSemaphoreDelete(_done);
    }

    WorkerTask(const WorkerTask&) = delete;
    WorkerTask& operator=(const WorkerTask&) = delete;

    [[nodiscard]] bool started() const { return _handle != nullptr; }

private:
    static void entry(void* arg) {
        auto* self = static_cast<WorkerTask*>(arg);
        self->_body(self->_ctx);
        // Warning: the owner may destroy *self as soon as _done is given.
        xSemaphoreGive(self->_done);
        vTaskDelete(nullptr);
    }

    Body _body;
    StressContext& _ctx;
    StaticSemaphore_t _done_storage{};
    SemaphoreHandle_t _done;
    TaskHandle_t _handle = nullptr;
};

bool runWorkers(AudioEngine& engine) {
    StressContext ctx(engine);
    std::vector<std::unique_ptr<WorkerTask>> workers;
    workers.push_back(std::make_unique<WorkerTask>("pair_ctrl", pairControllerBody, ctx, kPairPriority, kControllerCore));
    workers.push_back(std::make_unique<WorkerTask>("shot_ctrl", shotControllerBody, ctx, kShotPriority, tskNO_AFFINITY));
    workers.push_back(std::make_unique<WorkerTask>("stop_storm", stopStormBody, ctx, kStormPriority, tskNO_AFFINITY));
    if (kSdContention) {
        workers.push_back(
            std::make_unique<WorkerTask>("sd_contention", sdContentionBody, ctx, kContentionPriority, tskNO_AFFINITY));
    }
    workers.push_back(std::make_unique<WorkerTask>("report", reportBody, ctx, kReportPriority, tskNO_AFFINITY));

    const bool all_started =
        std::all_of(workers.begin(), workers.end(), [](const auto& worker) { return worker->started(); });
    if (all_started) {
        ESP_LOGI(TAG, "Running for %d s (start mode: %s, contention: %s)", CONFIG_STRESS_DURATION_S, kStartModeName,
                 kSdContention ? "on" : "off");
        vTaskDelay(pdMS_TO_TICKS(CONFIG_STRESS_DURATION_S * 1000));
    }

    xEventGroupSetBits(ctx.stop_event, kStopBit);
    workers.clear();
    logReport(ctx);
    return all_started;
}

bool allChannelsReusable(AudioEngine& engine) {
    std::vector<ChannelId> ids;
    ids.reserve(CONFIG_STRESS_MAX_CHANNELS);
    for (int i = 0; i < CONFIG_STRESS_MAX_CHANNELS; ++i) {
        const ChannelId id = engine.play(kLoopAPath, true, 0);
        if (id == INVALID_CHANNEL) {
            break;
        }
        ids.push_back(id);
    }

    const bool reusable = ids.size() == CONFIG_STRESS_MAX_CHANNELS;
    ESP_LOGI(TAG, "Quiesce check: %u of %d channels started", static_cast<unsigned>(ids.size()),
             CONFIG_STRESS_MAX_CHANNELS);
    for (const ChannelId id : ids) {
        engine.stop(id);
    }
    vTaskDelay(pdMS_TO_TICKS(kQuiesceSettleMs));
    return reusable;
}

void runStress() {
    SdCard sd({
        .mode = SdCard::HostMode::SDMMC_1BIT,
        .clk = BoardPins::kSdClk,
        .cmd = BoardPins::kSdCmd,
        .d0 = BoardPins::kSdD0,
        .mount_point = kMountPoint,
        .max_files = CONFIG_STRESS_MAX_CHANNELS + 2,
        .format_if_mount_failed = false,
    });
    if (const esp_err_t err = sd.init(); err != ESP_OK) {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(err));
        return;
    }

    if (const esp_err_t err = generateAssets(); err != ESP_OK) {
        ESP_LOGE(TAG, "Asset generation failed: %s", esp_err_to_name(err));
        return;
    }

    AudioEngine engine({
        .bclk_pin = BoardPins::kI2sBclk,
        .ws_pin = BoardPins::kI2sWs,
        .dout_pin = BoardPins::kI2sDout,
        .sd_mode_pin = BoardPins::kAmpSdMode,
        .max_channels = CONFIG_STRESS_MAX_CHANNELS,
    });
    if (const esp_err_t err = engine.init(); err != ESP_OK) {
        ESP_LOGE(TAG, "Audio engine init failed: %s", esp_err_to_name(err));
        return;
    }
    if (const esp_err_t err = engine.start(); err != ESP_OK) {
        ESP_LOGE(TAG, "Audio engine start failed: %s", esp_err_to_name(err));
        return;
    }

    const bool completed = runWorkers(engine);
    vTaskDelay(pdMS_TO_TICKS(kLongestShotMs + kQuiesceSettleMs));
    const bool pass = completed && allChannelsReusable(engine);
    if (pass) {
        ESP_LOGI(TAG, "PASS");
    } else {
        ESP_LOGE(TAG, "FAIL");
    }
}

} // namespace

extern "C" void app_main(void) {
    if (!kSdmmcSupported) {
        ESP_LOGE(TAG, "%s has no SDMMC host: this example needs an SDMMC 1-bit SD card.", CONFIG_IDF_TARGET);
        return;
    }
    runStress();
}
