// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>

#include "common/types.h"

namespace VideoCore {

/// nhlport: epoch for reusing stream-buffer copies of small read-only guest buffers
/// (NHLPORT_STREAM_REUSE=1, see BufferCache::ObtainBuffer). A copy is reused only while this value
/// is unchanged, so anything that can change guest memory, or the GPU-modified state of a range,
/// behind the command processor's back bumps it: emulator writes to guest memory
/// (TryWriteBacking: CP WRITE_DATA, readbacks, image downloads), every PM4 packet that is not a
/// register write or a draw, compute and constant-engine packets, writable buffer bindings, DMA
/// shaders, memory map changes and every command-processor coroutine switch.
inline std::atomic<u64> g_stream_reuse_epoch{1};

inline void BreakStreamReuse() noexcept {
    g_stream_reuse_epoch.fetch_add(1, std::memory_order_relaxed);
}

/// Reuse statistics, reported in the diag log summaries.
inline std::atomic<u64> g_stream_reuse_hits{0};
inline std::atomic<u64> g_stream_reuse_bytes{0};
inline std::atomic<u64> g_stream_copies{0};
inline std::atomic<u64> g_stream_copy_bytes{0};

} // namespace VideoCore
