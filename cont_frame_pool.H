/*
 File: cont_frame_pool.H

 Author: R. Bettati
 Department of Computer Science
 Texas A&M University
 Date  : 17/02/04 

 Description: Management of the CONTIGUOUS Free-Frame Pool.

 As opposed to SimpleFramePool, ContFramePool supports allocation and release of
 contiguous runs of frames.
*/

#ifndef _CONT_FRAME_POOL_H_
#define _CONT_FRAME_POOL_H_

#include "machine.H"

class ContFramePool {

private:
    /* ---- STATE MANAGEMENT ----
       We track each frame with a small state:
         Free         : available for allocation
         Used         : allocated, but NOT the first frame of the run
         HoS          : "Head of Sequence" (first frame of an allocated run)
         Inaccessible : reserved/unavailable (cannot be allocated)
    */
    enum class FrameState : unsigned char { Free=0, Used=1, HoS=2, Inaccessible=3 };

    // Pool range [base_frame_no, base_frame_no + n_frames)
    unsigned long base_frame_no;
    unsigned long n_frames;

    // Where management info is stored (frame number). If 0 => internal.
    unsigned long info_frame_no;

    // Bitmap: 2 bits per frame, packed (4 frames per byte)
    unsigned char* bitmap;
    unsigned long bitmap_bytes;

    // Static registry so release_frames() can find the owning pool.
    static const unsigned int MAX_POOLS = 32;
    static ContFramePool* pools[MAX_POOLS];
    static unsigned int pool_count;

    // Helpers
    inline bool owns(unsigned long frame_no) const {
        return (frame_no >= base_frame_no) && (frame_no < base_frame_no + n_frames);
    }
    inline unsigned long idx_of(unsigned long frame_no) const { return frame_no - base_frame_no; }

    FrameState get_state(unsigned long _frame_no);
    void set_state(unsigned long _frame_no, FrameState _state);

    void release_frames_impl(unsigned long _first_frame_no);

public:

    // The frame size is the same as the page size, duh...
    static const unsigned int FRAME_SIZE = Machine::PAGE_SIZE;

    ContFramePool(unsigned long _base_frame_no,
                  unsigned long _n_frames,
                  unsigned long _info_frame_no);
    /*
     Initializes the data structures needed for the management of this
     frame pool.
     _base_frame_no: Number of first frame managed by this frame pool.
     _n_frames: Size, in frames, of this frame pool.
     EXAMPLE: If _base_frame_no is 16 and _n_frames is 4, this frame pool manages
     physical frames numbered 16, 17, 18 and 19.
     _info_frame_no: Number of the first frame that should be used
     to store the management information for the frame pool.
     NOTE: If _info_frame_no is 0, the frame pool is free to
     choose any frames from the pool to store management information.
     NOTE: This function must be called before the paging system
     is initialized.
     */

    unsigned long get_frames(unsigned int _n_frames);
    /*
     Allocates a number of contiguous frames from the frame pool.
     _n_frames: Size of contiguous physical memory to allocate,
     in number of frames.
     If successful, returns the frame number of the first frame.
     If fails, returns 0.
     */

    void mark_inaccessible(unsigned long _base_frame_no,
                           unsigned long _n_frames);
    /*
     Marks a contiguous area of physical memory, i.e., a contiguous
     sequence of frames, as inaccessible.
     _base_frame_no: Number of first frame to mark as inaccessible.
     _n_frames: Number of contiguous frames to mark as inaccessible.
     */

    static void release_frames(unsigned long _first_frame_no);
    /*
     Releases a previously allocated contiguous sequence of frames
     back to its frame pool.
     The frame sequence is identified by the
     number of the first frame.
     NOTE: This function is static because there may be more than one frame pool
     defined in the system, and it is unclear which one this frame belongs to.
     This function must first identify the correct frame pool and then call the frame
     pool's release_frame function.
     */

    static unsigned long needed_info_frames(unsigned long _n_frames);
    /*
     Returns the number of frames needed to manage a frame pool of size _n_frames.
     The number returned here depends on the implementation of the frame pool and 
     on the frame size.
     */
};

#endif
