/*
 File: cont_frame_pool.C

 Implementation of ContFramePool (contiguous frame allocator).
*/

#include "cont_frame_pool.H"
#include "console.H"
#include "assert.H"

ContFramePool* ContFramePool::pools[ContFramePool::MAX_POOLS];
unsigned int   ContFramePool::pool_count = 0;

static inline unsigned long ceil_div(unsigned long a, unsigned long b) {
    return (a + b - 1) / b;
}

/* 2 bits per frame => ceil(n_frames / 4) bytes */
static inline unsigned long bitmap_bytes_for(unsigned long n_frames) {
    return ceil_div(n_frames, 4UL);
}

/* Compute number of frames required to store the bitmap externally */
unsigned long ContFramePool::needed_info_frames(unsigned long _n_frames)
{
    unsigned long bytes = bitmap_bytes_for(_n_frames);
    return ceil_div(bytes, (unsigned long)FRAME_SIZE);
}

ContFramePool::ContFramePool(unsigned long _base_frame_no,
                             unsigned long _n_frames,
                             unsigned long _info_frame_no)
{
    assert(_n_frames > 0);

    base_frame_no = _base_frame_no;
    n_frames      = _n_frames;
    info_frame_no = _info_frame_no;

    // Register this pool so release_frames() can find it.
    assert(pool_count < MAX_POOLS);
    pools[pool_count++] = this;

    bitmap_bytes = bitmap_bytes_for(n_frames);

    // If internal, store bitmap starting at base frame.
    if (info_frame_no == 0) {
        info_frame_no = base_frame_no;
    }

    // Management info must be usable before paging is enabled; in this MP, early memory is direct-mapped.
    bitmap = (unsigned char*)(info_frame_no * (unsigned long)FRAME_SIZE);

    // Initialize bitmap => all Free
    for (unsigned long i = 0; i < bitmap_bytes; i++) bitmap[i] = 0;

    // If internal, reserve bitmap storage frames as Inaccessible so they cannot be allocated.
    if (_info_frame_no == 0) {
        unsigned long info_frames = needed_info_frames(n_frames);
        for (unsigned long i = 0; i < info_frames && i < n_frames; i++) {
            set_state(base_frame_no + i, FrameState::Inaccessible);
        }
    }
}

/* ---- Bitmap accessor helpers (2 bits per frame, packed) ---- */
ContFramePool::FrameState ContFramePool::get_state(unsigned long _frame_no)
{
    assert(owns(_frame_no));
    unsigned long idx = idx_of(_frame_no);
    unsigned long byte_i = idx >> 2;             // /4
    unsigned long shift  = (idx & 0x3UL) << 1;   // (idx%4)*2
    unsigned char b = bitmap[byte_i];
    return (FrameState)((b >> shift) & 0x3U);
}

void ContFramePool::set_state(unsigned long _frame_no, FrameState _state)
{
    assert(owns(_frame_no));
    unsigned long idx = idx_of(_frame_no);
    unsigned long byte_i = idx >> 2;
    unsigned long shift  = (idx & 0x3UL) << 1;
    unsigned char mask = (unsigned char)(0x3U << shift);
    bitmap[byte_i] = (unsigned char)((bitmap[byte_i] & ~mask) | (((unsigned char)_state << shift) & mask));
}

/* ---- Allocation: first-fit scan for contiguous Free frames ---- */
unsigned long ContFramePool::get_frames(unsigned int _n_frames)
{
    if (_n_frames == 0 || _n_frames > n_frames) return 0;

    unsigned long run_start = 0;
    unsigned long run_len   = 0;

    for (unsigned long i = 0; i < n_frames; i++) {
        unsigned long frame_no = base_frame_no + i;
        FrameState st = get_state(frame_no);

        if (st == FrameState::Free) {
            if (run_len == 0) run_start = i;
            run_len++;

            if (run_len == _n_frames) {
                // Mark allocation: first frame is HoS, rest are Used
                set_state(base_frame_no + run_start, FrameState::HoS);
                for (unsigned long j = 1; j < _n_frames; j++) {
                    set_state(base_frame_no + run_start + j, FrameState::Used);
                }
                return base_frame_no + run_start;
            }
        } else {
            run_len = 0;
        }
    }
    return 0;
}

/* ---- Mark region as Inaccessible ---- */
void ContFramePool::mark_inaccessible(unsigned long _base_frame_no,
                                      unsigned long _n_frames)
{
    for (unsigned long f = _base_frame_no; f < _base_frame_no + _n_frames; f++) {
        if (!owns(f)) continue;
        set_state(f, FrameState::Inaccessible);
    }
}

/* ---- Release helpers ---- */
void ContFramePool::release_frames_impl(unsigned long _first_frame_no)
{
    assert(owns(_first_frame_no));
    assert(get_state(_first_frame_no) == FrameState::HoS);

    // Free head
    set_state(_first_frame_no, FrameState::Free);

    // Free following Used frames until the run ends.
    unsigned long f = _first_frame_no + 1;
    while (owns(f) && get_state(f) == FrameState::Used) {
        set_state(f, FrameState::Free);
        f++;
    }
}

/* Static release: find owning pool and release in that pool */
void ContFramePool::release_frames(unsigned long _first_frame_no)
{
    for (unsigned int i = 0; i < pool_count; i++) {
        ContFramePool* p = pools[i];
        if (p && p->owns(_first_frame_no)) {
            p->release_frames_impl(_first_frame_no);
            return;
        }
    }
    // No pool owns this frame => error.
    assert(false);
}
