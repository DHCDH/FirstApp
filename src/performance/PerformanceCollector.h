#pragma once

#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <set>
#include <functional>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================================
// Experiment configuration — defines parameter sweeps for CPU vs GPU comparison
// ============================================================================

enum class ExperimentID : uint32_t {
    EXP_INSTANCE_SCALING = 0,     // Vary N grinding wheel instances
    EXP_RESOLUTION_SCALING = 1,   // Vary render target resolution
    EXP_CANDIDATE_SCALING = 2,    // Vary UDF candidate radii count
    EXP_PLANE_SCALING = 3,        // Vary number of cross-section planes
    EXP_END_TO_END = 4,           // Full pipeline end-to-end
    EXP_COUNT
};

struct ExperimentConfig {
    ExperimentID id;
    std::string name;
    std::string description;
    std::string paramName;          // e.g. "num_instances"
    std::vector<uint32_t> paramValues;  // e.g. {100, 250, 500, 1000, 2000, 5000}
    uint32_t repetitions = 5;       // Number of repeated measurements per param
    uint32_t warmupRuns = 2;        // Warmup runs excluded from statistics

    // Fixed parameters
    uint32_t fixedResolution = 1024;
    uint32_t fixedInstances = 500;
    uint32_t fixedCandidates = 32;
    uint32_t fixedPlanes = 1;
};

// Pre-defined experiment configurations
inline std::vector<ExperimentConfig> GetDefaultExperiments() {
    return {
        {ExperimentID::EXP_INSTANCE_SCALING,
         "instance_scaling",
         "GPU instanced stencil rendering vs CPU analytical swept volume — vary wheel instance count",
         "num_instances",
         {100, 250, 500, 1000, 2000, 5000},
         5, 2,
         1024, 0, 32, 1},

        {ExperimentID::EXP_RESOLUTION_SCALING,
         "resolution_scaling",
         "Compute shader contour extraction pipeline — vary render resolution",
         "resolution",
         {256, 512, 1024, 2048, 4096},
         5, 2,
         0, 500, 32, 1},

        {ExperimentID::EXP_CANDIDATE_SCALING,
         "candidate_scaling",
         "UDF wear inversion — vary number of candidate radii",
         "num_candidates",
         {8, 16, 32, 64, 128},
         5, 2,
         1024, 500, 0, 1},

        {ExperimentID::EXP_PLANE_SCALING,
         "plane_scaling",
         "Multi-plane batch analysis throughput — vary cross-section count",
         "num_planes",
         {1, 5, 10, 20, 50},
         3, 1,
         1024, 500, 32, 0},

        {ExperimentID::EXP_END_TO_END,
         "end_to_end",
         "Full pipeline: mask→contour→features — GPU vs CPU end-to-end comparison",
         "num_planes",
         {1, 5, 10},
         3, 1,
         1024, 500, 32, 0},
    };
}

// ============================================================================
// Stage identifiers — fine-grained pipeline stages for both GPU and CPU
// ============================================================================

enum class PipelineStage : uint32_t {
    // --- GPU: Stencil rendering stages ---
    GPU_BLANK_STENCIL = 0,
    GPU_WHEEL_STENCIL_FRONT,
    GPU_WHEEL_STENCIL_BACK,
    GPU_STENCIL_RESOLVE,
    GPU_STENCIL_CLEAR,
    GPU_RENDER_TOTAL,

    // --- GPU: BBox scout pass ---
    GPU_BBOX_SCOUT,
    GPU_BBOX_READBACK,

    // --- GPU: Compute shader pipeline ---
    GPU_CONTOUR_EXTRACT,
    GPU_KNN_GRAPH,
    GPU_TRACE_SORT,
    GPU_ALIGN,
    GPU_RAKE_ANGLE,
    GPU_CORE_RADIUS,
    GPU_COMPUTE_TOTAL,

    // --- GPU: UDF wear inversion ---
    GPU_SDF_GENERATE,
    GPU_SDF_LOSS_COMPUTE,
    GPU_WEAR_INVERSION_TOTAL,

    // --- GPU: Pipeline overhead ---
    GPU_CMD_RECORD,
    GPU_FENCE_WAIT,
    GPU_TOTAL_FRAME,

    // --- CPU: Analytical swept volume ---
    CPU_SWEPT_VOLUME_ANALYTICAL,
    CPU_CONTOUR_EXTRACT,
    CPU_FEATURE_CALC,

    // --- CPU: UDF wear inversion ---
    CPU_SDF_GENERATE,
    CPU_SDF_LOSS,
    CPU_WEAR_INVERSION_TOTAL,

    // --- CPU: End-to-end ---
    CPU_TOTAL,

    STAGE_COUNT
};

inline const char* StageName(PipelineStage s) {
    switch (s) {
    case PipelineStage::GPU_BLANK_STENCIL:       return "gpu_blank_stencil";
    case PipelineStage::GPU_WHEEL_STENCIL_FRONT:  return "gpu_wheel_stencil_front";
    case PipelineStage::GPU_WHEEL_STENCIL_BACK:   return "gpu_wheel_stencil_back";
    case PipelineStage::GPU_STENCIL_RESOLVE:      return "gpu_stencil_resolve";
    case PipelineStage::GPU_STENCIL_CLEAR:        return "gpu_stencil_clear";
    case PipelineStage::GPU_RENDER_TOTAL:         return "gpu_render_total";
    case PipelineStage::GPU_BBOX_SCOUT:           return "gpu_bbox_scout";
    case PipelineStage::GPU_BBOX_READBACK:        return "gpu_bbox_readback";
    case PipelineStage::GPU_CONTOUR_EXTRACT:      return "gpu_contour_extract";
    case PipelineStage::GPU_KNN_GRAPH:            return "gpu_knn_graph";
    case PipelineStage::GPU_TRACE_SORT:           return "gpu_trace_sort";
    case PipelineStage::GPU_ALIGN:                return "gpu_align";
    case PipelineStage::GPU_RAKE_ANGLE:           return "gpu_rake_angle";
    case PipelineStage::GPU_CORE_RADIUS:          return "gpu_core_radius";
    case PipelineStage::GPU_COMPUTE_TOTAL:        return "gpu_compute_total";
    case PipelineStage::GPU_SDF_GENERATE:         return "gpu_sdf_generate";
    case PipelineStage::GPU_SDF_LOSS_COMPUTE:     return "gpu_sdf_loss_compute";
    case PipelineStage::GPU_WEAR_INVERSION_TOTAL: return "gpu_wear_inversion_total";
    case PipelineStage::GPU_CMD_RECORD:           return "gpu_cmd_record";
    case PipelineStage::GPU_FENCE_WAIT:           return "gpu_fence_wait";
    case PipelineStage::GPU_TOTAL_FRAME:          return "gpu_total_frame";
    case PipelineStage::CPU_SWEPT_VOLUME_ANALYTICAL: return "cpu_swept_volume_analytical";
    case PipelineStage::CPU_CONTOUR_EXTRACT:      return "cpu_contour_extract";
    case PipelineStage::CPU_FEATURE_CALC:         return "cpu_feature_calc";
    case PipelineStage::CPU_SDF_GENERATE:         return "cpu_sdf_generate";
    case PipelineStage::CPU_SDF_LOSS:             return "cpu_sdf_loss";
    case PipelineStage::CPU_WEAR_INVERSION_TOTAL: return "cpu_wear_inversion_total";
    case PipelineStage::CPU_TOTAL:                return "cpu_total";
    default: return "unknown";
    }
}

inline const char* StageUnit(PipelineStage s) {
    switch (s) {
    case PipelineStage::GPU_BLANK_STENCIL:       return "us";
    case PipelineStage::GPU_WHEEL_STENCIL_FRONT:  return "us";
    case PipelineStage::GPU_WHEEL_STENCIL_BACK:   return "us";
    case PipelineStage::GPU_STENCIL_RESOLVE:      return "us";
    case PipelineStage::GPU_STENCIL_CLEAR:        return "us";
    case PipelineStage::GPU_RENDER_TOTAL:         return "us";
    case PipelineStage::GPU_BBOX_SCOUT:           return "us";
    case PipelineStage::GPU_BBOX_READBACK:        return "us";
    case PipelineStage::GPU_CONTOUR_EXTRACT:      return "us";
    case PipelineStage::GPU_KNN_GRAPH:            return "us";
    case PipelineStage::GPU_TRACE_SORT:           return "us";
    case PipelineStage::GPU_ALIGN:                return "us";
    case PipelineStage::GPU_RAKE_ANGLE:           return "us";
    case PipelineStage::GPU_CORE_RADIUS:          return "us";
    case PipelineStage::GPU_COMPUTE_TOTAL:        return "us";
    case PipelineStage::GPU_SDF_GENERATE:         return "us";
    case PipelineStage::GPU_SDF_LOSS_COMPUTE:     return "us";
    case PipelineStage::GPU_WEAR_INVERSION_TOTAL: return "us";
    case PipelineStage::GPU_CMD_RECORD:           return "us";
    case PipelineStage::GPU_FENCE_WAIT:           return "us";
    case PipelineStage::GPU_TOTAL_FRAME:          return "us";
    default: return "us";
    }
}

// ============================================================================
// Timing record — a single measurement for one stage
// ============================================================================

struct TimingRecord {
    PipelineStage stage;
    uint64_t elapsedUs;         // elapsed time in microseconds
    uint32_t paramValue;        // the varied parameter value
    uint32_t repetition;        // which repetition (0-indexed)
    bool isWarmup;              // warmup run (excluded from aggregation)
    std::string source;         // "gpu" or "cpu"
};

// ============================================================================
// Aggregated statistics for a stage/param combination
// ============================================================================

struct StageStats {
    PipelineStage stage;
    uint32_t paramValue;
    uint32_t sampleCount;
    double meanUs;
    double stddevUs;
    double minUs;
    double maxUs;
    double medianUs;

    static StageStats Compute(const std::vector<TimingRecord>& records,
                               PipelineStage stage, uint32_t paramValue) {
        std::vector<uint64_t> vals;
        for (auto& r : records) {
            if (r.stage == stage && r.paramValue == paramValue && !r.isWarmup) {
                vals.push_back(r.elapsedUs);
            }
        }
        StageStats s{};
        s.stage = stage;
        s.paramValue = paramValue;
        s.sampleCount = static_cast<uint32_t>(vals.size());
        if (vals.empty()) return s;

        std::sort(vals.begin(), vals.end());
        s.minUs = static_cast<double>(vals.front());
        s.maxUs = static_cast<double>(vals.back());
        double sum = 0.0;
        for (auto v : vals) sum += static_cast<double>(v);
        s.meanUs = sum / vals.size();

        double sqSum = 0.0;
        for (auto v : vals) {
            double d = static_cast<double>(v) - s.meanUs;
            sqSum += d * d;
        }
        s.stddevUs = std::sqrt(sqSum / vals.size());

        if (vals.size() % 2 == 0) {
            s.medianUs = (vals[vals.size() / 2 - 1] + vals[vals.size() / 2]) / 2.0;
        } else {
            s.medianUs = static_cast<double>(vals[vals.size() / 2]);
        }
        return s;
    }
};

// ============================================================================
// Scoped CPU timer — RAII timer for CPU code paths
// ============================================================================

class ScopedCpuTimer {
public:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;

    ScopedCpuTimer(PipelineStage stage, uint32_t paramValue, uint32_t rep,
                   bool warmup, std::vector<TimingRecord>& sink)
        : m_stage(stage), m_paramValue(paramValue), m_rep(rep),
          m_warmup(warmup), m_sink(&sink) {
        m_start = Clock::now();
    }

    ~ScopedCpuTimer() {
        auto end = Clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - m_start).count();
        m_sink->push_back({m_stage, static_cast<uint64_t>(us),
                           m_paramValue, m_rep, m_warmup, "cpu"});
    }

    ScopedCpuTimer(const ScopedCpuTimer&) = delete;
    ScopedCpuTimer& operator=(const ScopedCpuTimer&) = delete;

private:
    PipelineStage m_stage;
    uint32_t m_paramValue;
    uint32_t m_rep;
    bool m_warmup;
    std::vector<TimingRecord>* m_sink;
    TimePoint m_start;
};

// ============================================================================
// GPU Timestamp Query Pool — for measuring GPU execution time on the device
// ============================================================================

class GpuTimestampPool {
public:
    GpuTimestampPool() = default;

    void Initialize(VkDevice device, uint32_t maxTimestamps) {
        m_device = device;
        m_maxTimestamps = maxTimestamps;
        m_timestampPeriod = 0.0f;

        VkQueryPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        poolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        poolInfo.queryCount = maxTimestamps;
        vkCreateQueryPool(device, &poolInfo, nullptr, &m_pool);

        m_results.resize(maxTimestamps);
    }

    void Destroy() {
        if (m_pool != VK_NULL_HANDLE) {
            vkDestroyQueryPool(m_device, m_pool, nullptr);
            m_pool = VK_NULL_HANDLE;
        }
    }

    void SetTimestampPeriod(float period) { m_timestampPeriod = period; }
    float GetTimestampPeriod() const { return m_timestampPeriod; }

    VkQueryPool GetPool() const { return m_pool; }
    uint32_t GetMaxTimestamps() const { return m_maxTimestamps; }

    // Write a timestamp at the current point in the command buffer
    void WriteTimestamp(VkCommandBuffer cmd, VkPipelineStageFlagBits stage,
                        uint32_t queryIndex) {
        vkCmdWriteTimestamp(cmd, stage, m_pool, queryIndex);
    }

    // Read back all timestamps after GPU work completes
    void ReadResults(uint32_t firstQuery, uint32_t queryCount) {
        if (queryCount == 0) return;
        vkGetQueryPoolResults(m_device, m_pool, firstQuery, queryCount,
                              queryCount * sizeof(uint64_t),
                              m_results.data(), sizeof(uint64_t),
                              VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    }

    // Convert timestamp difference to microseconds
    double TimestampDeltaUs(uint32_t startIdx, uint32_t endIdx) const {
        if (startIdx >= m_maxTimestamps || endIdx >= m_maxTimestamps) return 0.0;
        uint64_t diff = m_results[endIdx] - m_results[startIdx];
        return static_cast<double>(diff) * static_cast<double>(m_timestampPeriod) / 1000.0;
    }

    // Raw timestamp in us
    double TimestampUs(uint32_t idx) const {
        if (idx >= m_maxTimestamps) return 0.0;
        return static_cast<double>(m_results[idx]) *
               static_cast<double>(m_timestampPeriod) / 1000.0;
    }

    void Reset(VkCommandBuffer cmd) {
        vkCmdResetQueryPool(cmd, m_pool, 0, m_maxTimestamps);
    }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueryPool m_pool = VK_NULL_HANDLE;
    uint32_t m_maxTimestamps = 0;
    float m_timestampPeriod = 0.0f;
    std::vector<uint64_t> m_results;
};

// ============================================================================
// CSV Writer — outputs experiment data in tidy (long-format) CSV
// ============================================================================

class CsvWriter {
public:
    explicit CsvWriter(const std::string& filepath)
        : m_filepath(filepath) {}

    void WriteHeader() {
        std::ofstream f(m_filepath);
        f << "experiment_id,experiment_name,param_name,param_value,"
          << "stage,stage_name,source,repetition,is_warmup,elapsed_us\n";
    }

    void AppendRecords(const std::string& expId, const std::string& expName,
                       const std::string& paramName,
                       const std::vector<TimingRecord>& records) {
        std::ofstream f(m_filepath, std::ios::app);
        f << std::fixed << std::setprecision(3);
        for (auto& r : records) {
            f << expId << ","
              << expName << ","
              << paramName << ","
              << r.paramValue << ","
              << static_cast<uint32_t>(r.stage) << ","
              << StageName(r.stage) << ","
              << r.source << ","
              << r.repetition << ","
              << (r.isWarmup ? 1 : 0) << ","
              << r.elapsedUs << "\n";
        }
    }

    void WriteStats(const std::string& expId, const std::string& expName,
                    const std::string& paramName,
                    const std::vector<StageStats>& stats) {
        std::string statsPath = m_filepath;
        size_t dot = statsPath.rfind('.');
        if (dot != std::string::npos) {
            statsPath = statsPath.substr(0, dot) + "_stats.csv";
        } else {
            statsPath += "_stats";
        }

        std::ofstream f(statsPath);
        f << std::fixed << std::setprecision(3);
        f << "experiment_id,experiment_name,param_name,param_value,"
          << "stage,stage_name,source,samples,"
          << "mean_us,stddev_us,min_us,max_us,median_us\n";

        for (auto& s : stats) {
            f << expId << ","
              << expName << ","
              << paramName << ","
              << s.paramValue << ","
              << static_cast<uint32_t>(s.stage) << ","
              << StageName(s.stage) << ","
              << "gpu" << ","  // source derived from stage enum prefix
              << s.sampleCount << ","
              << s.meanUs << ","
              << s.stddevUs << ","
              << s.minUs << ","
              << s.maxUs << ","
              << s.medianUs << "\n";
        }
    }

private:
    std::string m_filepath;
};

// ============================================================================
// PerformanceCollector — central singleton for experiment orchestration
// ============================================================================

class PerformanceCollector {
public:
    static PerformanceCollector& Instance() {
        static PerformanceCollector instance;
        return instance;
    }

    // --- Configuration ---
    void SetOutputDir(const std::string& dir) { m_outputDir = dir; }
    void SetExperiment(const ExperimentConfig& cfg) { m_currentConfig = cfg; }

    // --- Record management ---
    void ClearRecords() { m_records.clear(); }
    void AddRecord(const TimingRecord& r) { m_records.push_back(r); }
    void AddCpuTiming(PipelineStage stage, uint64_t elapsedUs, uint32_t paramVal,
                      uint32_t rep, bool warmup) {
        m_records.push_back({stage, elapsedUs, paramVal, rep, warmup, "cpu"});
    }
    void AddGpuTiming(PipelineStage stage, uint64_t elapsedUs, uint32_t paramVal,
                      uint32_t rep, bool warmup) {
        m_records.push_back({stage, elapsedUs, paramVal, rep, warmup, "gpu"});
    }

    // --- GPU timestamp integration ---
    void RecordGpuTimestamps(const std::vector<std::pair<PipelineStage, double>>& stageTimesUs,
                             uint32_t paramVal, uint32_t rep, bool warmup) {
        for (auto& [stage, timeUs] : stageTimesUs) {
            m_records.push_back({stage, static_cast<uint64_t>(timeUs),
                                 paramVal, rep, warmup, "gpu"});
        }
    }

    // --- Statistics ---
    std::vector<StageStats> ComputeStats() const {
        std::vector<StageStats> result;
        // Group by (stage, paramValue)
        std::set<std::pair<uint32_t, uint32_t>> groups;
        for (auto& r : m_records) {
            groups.insert({static_cast<uint32_t>(r.stage), r.paramValue});
        }
        for (auto& [s, pv] : groups) {
            result.push_back(StageStats::Compute(m_records, static_cast<PipelineStage>(s), pv));
        }
        return result;
    }

    // --- CSV export ---
    void ExportCsv(const std::string& experimentName) {
        std::string path = m_outputDir + "/" + experimentName + ".csv";
        CsvWriter writer(path);
        writer.WriteHeader();
        writer.AppendRecords(experimentName, m_currentConfig.name,
                            m_currentConfig.paramName, m_records);

        auto stats = ComputeStats();
        writer.WriteStats(experimentName, m_currentConfig.name,
                         m_currentConfig.paramName, stats);
        std::cout << "[PerfCollector] Exported " << m_records.size()
                  << " records to " << path << "\n";
    }

    // --- Speedup computation ---
    static double ComputeSpeedup(double cpuTimeUs, double gpuTimeUs) {
        if (gpuTimeUs <= 0.0) return 0.0;
        return cpuTimeUs / gpuTimeUs;
    }

    const std::vector<TimingRecord>& GetRecords() const { return m_records; }

private:
    PerformanceCollector() = default;
    std::string m_outputDir = "D:/Data/Study/vulkan/FirstApp/output_stuff";
    ExperimentConfig m_currentConfig{};
    std::vector<TimingRecord> m_records;
};

// ============================================================================
// Convenience macros for CPU timing
// ============================================================================

#define PERF_CPU_TIMER_START(var, stage, paramVal, rep, warmup) \
    auto var##_t0 = std::chrono::high_resolution_clock::now()

#define PERF_CPU_TIMER_END(var, stage, paramVal, rep, warmup) \
    do { \
        auto var##_t1 = std::chrono::high_resolution_clock::now(); \
        auto var##_us = std::chrono::duration_cast<std::chrono::microseconds>( \
            var##_t1 - var##_t0).count(); \
        PerformanceCollector::Instance().AddCpuTiming( \
            stage, static_cast<uint64_t>(var##_us), paramVal, rep, warmup); \
    } while(0)
