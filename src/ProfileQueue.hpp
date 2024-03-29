/**
 * Copyright (c) JoeScan Inc. All Rights Reserved.
 *
 * Licensed under the BSD 3 Clause License. See LICENSE.txt in the project
 * root for license information.
 */

#ifndef JOESCAN_PROFILE_QUEUE_H
#define JOESCAN_PROFILE_QUEUE_H

#include "ScanHeadModel.hpp"
#include "CameraLaserIterator.hpp"
#include "joescan_pinchot.h"

#include "readerwritercircularbuffer.h"

#include <map>
#include <utility>

namespace joescan {

using moodycamel::BlockingReaderWriterCircularBuffer;

class ProfileQueue {
  /**
   * There are two ways that the `ProfileQueue` operates, in "single queue"
   * mode and "multi queue" mode. The former is used when the user is pulling
   * profiles out from each scan head individually; the latter is used for
   * frame scanning. For "singe queue" mode, all the profiles generated by
   * the scan head end up in one queue. For "multi queue" mode, the profiles
   * are placed into queues determined by their camera/laser pair.
   */

 private:

  struct Queue {
    // queue of pointers to profiles free to use for incoming profiles
    BlockingReaderWriterCircularBuffer<jsRawProfile*> free;
    // queue of pointers to profiles ready to be read out by user
    BlockingReaderWriterCircularBuffer<jsRawProfile*> ready;
    uint32_t last_sequence;

    Queue(uint32_t size) :
      free(size),
      ready(size),
      last_sequence(0)
    { }
  };

  const uint32_t kMaxProfilesQueue = JS_SCAN_HEAD_PROFILES_MAX;

  // static memory allocated for buffering profiles per scan head
  ScanHeadModel &m_model;
  std::vector<jsRawProfile> m_profiles;
  std::map<std::pair<jsCamera, jsLaser>, Queue> m_element_queue;
  std::vector<std::pair<jsCamera, jsLaser>> m_valid_pairs;
  Queue m_single_queue;
  bool m_is_single_queue;

  inline int EnqueueInternal(
    BlockingReaderWriterCircularBuffer<jsRawProfile*> &q,
    jsRawProfile **p)
  {
    if (q.try_enqueue(*p)) {
      return 0;
    }
    return -1;
  }

  inline int DequeueInternal(
    BlockingReaderWriterCircularBuffer<jsRawProfile*> &q,
    jsRawProfile **p)
  {
    if (q.try_dequeue(*p)) {
      return 0;
    }
    return -1;
  }

  inline int32_t ReadyPeekSequenceInternal(
    BlockingReaderWriterCircularBuffer<jsRawProfile*> &q,
    uint32_t *seq)
  {
    auto ptr = q.peek();
    if (nullptr == ptr) {
      return -1;
    }
    jsRawProfile *raw = *ptr;
    *seq = raw->sequence_number;
    return 0;
  }

  inline uint32_t SizeInternal(
    BlockingReaderWriterCircularBuffer<jsRawProfile*> &q)
  {
    return (uint32_t) q.size_approx();
  }

 public:
  struct Report {
    uint32_t size_min;
    uint32_t size_max;
    uint32_t sequence_min;
    uint32_t sequence_max;
  };

  enum Mode {
    MODE_SINGLE,
    MODE_MULTI
  };

  ProfileQueue(ScanHeadModel &model) :
    m_model(model), m_single_queue(kMaxProfilesQueue)
  {
    m_profiles.resize(JS_SCAN_HEAD_PROFILES_MAX);

    auto iter = CameraLaserIterator(m_model);
    uint32_t size = kMaxProfilesQueue / iter.count();
    for (auto &pair : iter) {
      m_element_queue.emplace(
        std::make_pair(pair, Queue(size)));
    }
  }

  ~ProfileQueue() = default;

  void Reset(Mode mode)
  {
    // Resets the internal memory and also configures the queues according to
    // the requested mode. This will be called at the beginning of scanning
    // to get everything in a clean state or if the user wants to flush old
    // data out from the API.

    while (true == m_single_queue.ready.try_pop());
    while (true == m_single_queue.free.try_pop());

    auto iter = CameraLaserIterator(m_model);
    for (auto &pair : iter) {
      Queue *queue = &(m_element_queue.at(pair));
      while (true == queue->ready.try_pop());
      while (true == queue->free.try_pop());
    }

    if (MODE_SINGLE == mode) {
      m_is_single_queue = true;
      m_single_queue.last_sequence = 0;

      for (uint32_t n = 0; n < kMaxProfilesQueue; n++) {
        jsRawProfile *p = &m_profiles[n];
        jsRawProfileInit(p);
        bool r = m_single_queue.free.try_enqueue(p);
        assert(true == r);
      }
    } else if (MODE_MULTI == mode) {
      m_is_single_queue = false;

      uint32_t m = 0;
      iter = CameraLaserIterator(m_model);
      uint32_t size = kMaxProfilesQueue / iter.count();
      for (auto &pair : iter) {
        Queue *queue = &(m_element_queue.at(pair));
        queue->last_sequence = 0;

        for (uint32_t n = 0; n < size; n++) {
          jsRawProfile *p = &m_profiles[m++];
          jsRawProfileInit(p);
          queue->free.try_enqueue(p);
        }
      }
    }
  }

  int32_t EnqueueFree(jsRawProfile **p)
  {
    return EnqueueInternal(m_single_queue.free, p);
  }

  int32_t EnqueueFree(jsCamera camera, jsLaser laser, jsRawProfile **p)
  {
    auto pair = std::make_pair(camera, laser);
    return EnqueueInternal(m_element_queue.at(pair).free, p);
  }

  int32_t DequeueFree(jsRawProfile **p)
  {
    return DequeueInternal(m_single_queue.free, p);
  }

  int32_t DequeueFree(jsCamera camera, jsLaser laser, jsRawProfile **p)
  {
    auto pair = std::make_pair(camera, laser);
    return DequeueInternal(m_element_queue.at(pair).free, p);
  }

  int32_t EnqueueReady(jsRawProfile **p)
  {
    int32_t r = 0;
    r = EnqueueInternal(m_single_queue.ready, p);
    if ((0 == r) && (m_single_queue.last_sequence < (*p)->sequence_number)) {
      m_single_queue.last_sequence = (*p)->sequence_number;
    }

    return r;
  }

  int32_t EnqueueReady(jsCamera camera, jsLaser laser, jsRawProfile **p)
  {
    const auto pair = std::make_pair(camera, laser);
    Queue *queue = &(m_element_queue.at(pair));
    int32_t r = 0;

    r = EnqueueInternal(queue->ready, p);
    if ((0 == r) && (queue->last_sequence < (*p)->sequence_number)) {
      queue->last_sequence = (*p)->sequence_number;
    }

    return r;
  }

  int32_t DequeueReady(jsRawProfile **p)
  {
    return DequeueInternal(m_single_queue.ready, p);
  }

  int32_t DequeueReady(jsCamera camera, jsLaser laser, jsRawProfile **p)
  {
    const auto pair = std::make_pair(camera, laser);
    return DequeueInternal(m_element_queue.at(pair).ready, p);
  }

  int32_t ReadyPeekSequence(jsCamera camera, jsLaser laser, uint32_t *seq)
  {
    const auto pair = std::make_pair(camera, laser);
    return ReadyPeekSequenceInternal(m_element_queue.at(pair).ready, seq);
  }

  void SetValidPairs(const std::vector<std::pair<jsCamera, jsLaser>> &pairs)
  {
    m_valid_pairs = pairs;
  }

  Report GetReport() {
    Report report;

    if (m_is_single_queue) {
      uint32_t seq = m_single_queue.last_sequence;
      report.sequence_min = seq;
      report.sequence_max = seq;
      uint32_t sz = (uint32_t) m_single_queue.ready.size_approx();
      report.size_min = sz;
      report.size_max = sz;
    } else {
      // Use `int64_t` set to `-1` to start at value outside `uint32_t` and
      // still be large enough to hold all `uint32_t` values. 
      int64_t seq_min = -1;
      int64_t seq_max = -1;
      int64_t sz_min = -1;
      int64_t sz_max = -1;

      for (auto &pair : m_valid_pairs) {
        Queue *queue = &(m_element_queue.at(pair));
        uint32_t seq = queue->last_sequence;

        if ((-1 == seq_min) || (seq_min > seq)) seq_min = seq;
        if ((-1 == seq_max) || (seq_max < seq)) seq_max = seq;

        uint32_t sz = (uint32_t) queue->ready.size_approx();
        if ((-1 == sz_min) || (sz_min > sz)) sz_min = sz;
        if ((-1 == sz_max) || (sz_max < sz)) sz_max = sz;
      }

      report.sequence_min = (uint32_t) seq_min;
      report.sequence_max = (uint32_t) seq_max;
      report.size_min = (uint32_t) sz_min;
      report.size_max = (uint32_t) sz_max;
    }

    return report;
  }

  uint32_t SizeFree()
  {
    return SizeInternal(m_single_queue.free);
  }

  uint32_t SizeFree(jsCamera camera, jsLaser laser)
  {
    const auto pair = std::make_pair(camera, laser);
    return SizeInternal(m_element_queue.at(pair).free);
  }

  uint32_t SizeReady()
  {
    return SizeInternal(m_single_queue.ready);
  }

  uint32_t SizeReady(jsCamera camera, jsLaser laser)
  {
    const auto pair = std::make_pair(camera, laser);
    return SizeInternal(m_element_queue.at(pair).ready);
  }
};
}

#endif
