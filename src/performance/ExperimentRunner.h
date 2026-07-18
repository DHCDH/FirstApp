#pragma once

#include <functional>
#include <iostream>
#include <string>

#include "../Global.h"
#include "PerformanceCollector.h"

// ============================================================================
// ExperimentRunner — orchestrates parameter sweeps and collects timing data
// ============================================================================

class ExperimentRunner {
public:
    ExperimentRunner() {
        PerformanceCollector::Instance().SetOutputDir(
            "D:/Data/Study/vulkan/FirstApp/output_stuff");
    }

    // Callback types: the caller provides functions that run one trial
    using GpuTrialFn = std::function<void(uint32_t paramValue, uint32_t rep, bool warmup)>;
    using CpuTrialFn = std::function<void(uint32_t paramValue, uint32_t rep, bool warmup)>;

    void RunExperiment(const ExperimentConfig& cfg,
                       GpuTrialFn gpuFn,
                       CpuTrialFn cpuFn) {
        auto& collector = PerformanceCollector::Instance();
        collector.SetExperiment(cfg);
        collector.ClearRecords();

        std::cout << "\n========== Experiment: " << cfg.name << " ==========\n";
        std::cout << "Description: " << cfg.description << "\n";
        std::cout << "Parameter: " << cfg.paramName << "\n";
        std::cout << "Values: ";
        for (auto v : cfg.paramValues) std::cout << v << " ";
        std::cout << "\nRepetitions: " << cfg.repetitions
                  << " (+" << cfg.warmupRuns << " warmup)\n\n";

        for (auto paramVal : cfg.paramValues) {
            std::cout << "--- " << cfg.paramName << " = " << paramVal << " ---\n";

            uint32_t totalRuns = cfg.warmupRuns + cfg.repetitions;
            for (uint32_t r = 0; r < totalRuns; r++) {
                bool warmup = (r < cfg.warmupRuns);

                // GPU trial
                if (gpuFn) {
                    gpuFn(paramVal, r, warmup);
                }

                // CPU trial
                if (cpuFn) {
                    cpuFn(paramVal, r, warmup);
                }

                if (!warmup) {
                    std::cout << "  Rep " << (r - cfg.warmupRuns + 1)
                              << "/" << cfg.repetitions << " done\n";
                }
            }
        }

        // Export
        collector.ExportCsv(cfg.name);

        // Print summary
        PrintSummary(cfg);
    }

    void PrintSummary(const ExperimentConfig& cfg) {
        auto& collector = PerformanceCollector::Instance();
        auto stats = collector.ComputeStats();

        std::cout << "\n========== Summary: " << cfg.name << " ==========\n";
        std::cout << std::fixed << std::setprecision(1);

        // Group stats by param value, find GPU total and CPU total for speedup
        for (auto pv : cfg.paramValues) {
            double gpuTotal = 0.0, cpuTotal = 0.0;
            for (auto& s : stats) {
                if (s.paramValue != pv) continue;
                if (s.stage == PipelineStage::GPU_TOTAL_FRAME) gpuTotal = s.meanUs;
                if (s.stage == PipelineStage::CPU_TOTAL) cpuTotal = s.meanUs;
            }
            double speedup = PerformanceCollector::ComputeSpeedup(cpuTotal, gpuTotal);
            std::cout << cfg.paramName << "=" << pv
                      << " | GPU: " << gpuTotal << " us"
                      << " | CPU: " << cpuTotal << " us"
                      << " | Speedup: " << speedup << "x\n";
        }
        std::cout << "==========================================\n\n";
    }
};
