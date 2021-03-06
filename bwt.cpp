/*
  Copyright (c) 2015 Genome Research Ltd.

  Author: Jouni Siren <jouni.siren@iki.fi>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include <condition_variable>

#include "bwt.h"

namespace bwtmerge
{

//------------------------------------------------------------------------------

BWT::BWT()
{
}

BWT::BWT(const BWT& source)
{
  this->copy(source);
}

BWT::BWT(BWT&& source)
{
  *this = std::move(source);
}

BWT::~BWT()
{
}

void
BWT::copy(const BWT& source)
{
  this->header = source.header;

  this->data = source.data;
  for(size_type c = 0; c < SIGMA; c++) { this->samples[c] = source.samples[c]; }

  this->block_boundaries = source.block_boundaries;
  this->block_rank = source.block_rank;
  this->block_select = source.block_select;
  this->setVectors();
}

void
BWT::setVectors()
{
  this->block_rank.set_vector(&(this->block_boundaries));
  this->block_select.set_vector(&(this->block_boundaries));
}

void
BWT::swap(BWT& source)
{
  if(this != &source)
  {
    std::swap(this->header, source.header);
    this->data.swap(source.data);
    for(size_type c = 0; c < SIGMA; c++) { this->samples[c].swap(source.samples[c]); }
    this->block_boundaries.swap(source.block_boundaries);
    sdsl::util::swap_support(this->block_rank, source.block_rank, &(this->block_boundaries), &(source.block_boundaries));
    sdsl::util::swap_support(this->block_select, source.block_select, &(this->block_boundaries), &(source.block_boundaries));
  }
}

BWT&
BWT::operator=(const BWT& source)
{
  if(this != &source) { this->copy(source); }
  return *this;
}

BWT&
BWT::operator=(BWT&& source)
{
  if(this != &source)
  {
    this->header = std::move(source.header);
    this->data = std::move(source.data);
    for(size_type c = 0; c < SIGMA; c++) { this->samples[c] = std::move(source.samples[c]); }

    this->block_boundaries = std::move(source.block_boundaries);
    this->block_rank = std::move(source.block_rank);
    this->block_select = std::move(source.block_select);
    this->setVectors();
  }
  return *this;
}

BWT::size_type
BWT::serialize(std::ostream& out, sdsl::structure_tree_node* v, std::string name) const
{
  sdsl::structure_tree_node* child = sdsl::structure_tree::add_child(v, name, sdsl::util::class_name(*this));
  size_type written_bytes = 0;

  written_bytes += this->header.serialize(out, child, "header");
  written_bytes += this->data.serialize(out, child, "data");
  for(size_type c = 0; c < SIGMA; c++)
  {
    std::stringstream ss; ss << "samples_" << c;
    written_bytes += this->samples[c].serialize(out, child, ss.str());
  }
  written_bytes += this->block_boundaries.serialize(out, child, "block_boundaries");
  written_bytes += this->block_rank.serialize(out, child, "block_rank");
  written_bytes += this->block_select.serialize(out, child, "block_select");

  sdsl::structure_tree::add_size(child, written_bytes);
  return written_bytes;
}

void
BWT::load(std::istream& in)
{
  this->header.load(in);
  if(!(this->header.check()))
  {
    std::cerr << "BWT::load(): Invalid header!" << std::endl;
    std::exit(EXIT_FAILURE);
  }

  this->data.load(in);
  for(size_type c = 0; c < SIGMA; c++) { this->samples[c].load(in); }

  this->block_boundaries.load(in);
  this->block_rank.load(in, &(this->block_boundaries));
  this->block_select.load(in, &(this->block_boundaries));
}

//------------------------------------------------------------------------------

struct RABuffer
{
  typedef RankArray::run_type run_type;

  const static size_type BUFFER_SIZE = MEGABYTE;  // Runs.

  std::mutex              mtx;
  std::condition_variable full, empty;
  bool                    finished;
  std::vector<run_type>   buffer;

  RABuffer()
  {
    this->buffer.reserve(BUFFER_SIZE);
    this->finished = false;
  }

  ~RABuffer()
  {
  }

  void get(std::vector<run_type>& out_buffer, bool& last)
  {
    std::unique_lock<std::mutex> lock(this->mtx);
    this->full.wait(lock, [this]() { return !(buffer.empty()); } );
    out_buffer.swap(this->buffer);
    last = this->finished;
    this->empty.notify_one();
  }

  void add(std::vector<run_type>& in_buffer, bool last)
  {
    std::unique_lock<std::mutex> lock(this->mtx);
    this->empty.wait(lock, [this]() { return buffer.empty(); } );
    this->buffer.swap(in_buffer);
    this->finished = last;
    this->full.notify_one();
  }
};

//------------------------------------------------------------------------------

void
mergeRA(RankArray& ra, RABuffer& ra_buffer)
{
  std::vector<RankArray::run_type> out_buffer;
  out_buffer.reserve(RABuffer::BUFFER_SIZE);

  RunBuffer run_buffer;
  for(ra.open(); !(ra.end()); ++ra)
  {
    if(run_buffer.add(*ra))
    {
      out_buffer.push_back(run_buffer.run);
      if(out_buffer.size() >= RABuffer::BUFFER_SIZE) { ra_buffer.add(out_buffer, ra.end()); }
    }
  }
  run_buffer.flush(); out_buffer.push_back(run_buffer.run);
  if(out_buffer.size() > 0) { ra_buffer.add(out_buffer, ra.end()); }

  ra.close();
}

void
mergeBWT(BWT& a, BWT& b, BWT& result, sdsl::int_vector<64>& counts, RABuffer& ra_buffer)
{
  std::vector<RABuffer::run_type> in_buffer;
  in_buffer.reserve(RABuffer::BUFFER_SIZE);
  RunBuffer out_buffer;
  bool ra_finished = false;
  size_type a_rle_pos = 0, b_rle_pos = 0;
  size_type a_seq_pos = 0;
  range_type a_run = Run::read(a.data, a_rle_pos); a.data.clearUntil(a_rle_pos);
  range_type b_run = Run::read(b.data, b_rle_pos); b.data.clearUntil(b_rle_pos);

  // Interleave a and b according to ra.
  while(!ra_finished)
  {
    ra_buffer.get(in_buffer, ra_finished);
    for(size_type i = 0; i < in_buffer.size(); i++)
    {
      RankArray::run_type curr = in_buffer[i];
      while(a_seq_pos < curr.first)
      {
        size_type length = std::min(curr.first - a_seq_pos, a_run.second);
        if(out_buffer.add(a_run.first, length))
        {
          Run::write(result.data, out_buffer.run);
          counts[out_buffer.run.first] += out_buffer.run.second;
        }
        a_run.second -= length; a_seq_pos += length;
        if(a_run.second == 0 && a_rle_pos < a.data.size())
        {
          a_run = Run::read(a.data, a_rle_pos); a.data.clearUntil(a_rle_pos);
        }
      }
      while(curr.second > 0)
      {
        size_type length = std::min(curr.second, b_run.second);
        if(out_buffer.add(b_run.first, length))
        {
          Run::write(result.data, out_buffer.run);
          counts[out_buffer.run.first] += out_buffer.run.second;
        }
        b_run.second -= length; curr.second -= length;
        if(b_run.second == 0 && b_rle_pos < b.data.size())
        {
          b_run = Run::read(b.data, b_rle_pos); b.data.clearUntil(b_rle_pos);
        }
      }
    }
    in_buffer.clear();
  }

  // Append the rest of a.
  while(a_run.second > 0)
  {
    if(out_buffer.add(a_run))
    {
      Run::write(result.data, out_buffer.run);
      counts[out_buffer.run.first] += out_buffer.run.second;
    }
    if(a_rle_pos < a.data.size()) { a_run = Run::read(a.data, a_rle_pos); a.data.clearUntil(a_rle_pos); }
    else { a_run.second = 0; }
  }

  // Flush the buffer.
  out_buffer.flush();
  Run::write(result.data, out_buffer.run);
  counts[out_buffer.run.first] += out_buffer.run.second;
}

//------------------------------------------------------------------------------

BWT::BWT(BWT& a, BWT& b, RankArray& ra)
{
#ifdef VERBOSE_STATUS_INFO
  double start = readTimer();
#endif

  a.destroy(); b.destroy();
  RABuffer ra_buffer;
  sdsl::int_vector<64> counts(SIGMA, 0);

  std::thread producer(mergeRA, std::ref(ra), std::ref(ra_buffer));
  mergeBWT(a, b, *this, counts, ra_buffer);
  producer.join();

#ifdef VERBOSE_STATUS_INFO
  double midpoint = readTimer();
  std::cerr << "bwt_merge: BWTs merged in " << (midpoint - start) << " seconds" << std::endl;
#endif

  this->header.sequences = a.sequences() + b.sequences();
  this->header.bases = a.size() + b.size();
  this->header.setOrder(a.header.order());
  this->build(counts);

#ifdef VERBOSE_STATUS_INFO
  double seconds = readTimer() - midpoint;
  std::cerr << "bwt_merge: rank/select built in " << seconds << " seconds" << std::endl;
#endif
}

//------------------------------------------------------------------------------

size_type
BWT::rank(size_type i, comp_type c) const
{
  if(c >= SIGMA) { return 0; }
  if(i > this->size()) { i = this->size(); }

  size_type block = this->block_rank(i);
  size_type res = this->samples[c].sum(block);
  size_type rle_pos = block * SAMPLE_RATE;
  size_type seq_pos = (block > 0 ? this->block_select(block) + 1 : 0);

  while(seq_pos < i)
  {
    range_type run = Run::read(this->data, rle_pos);
    seq_pos += run.second;  // The starting position of the next run.
    if(run.first == c)
    {
      res += run.second;  // Number of c's before the next run.
      if(seq_pos > i) { res -= seq_pos - i; }
    }
  }

  return res;
}

void
BWT::ranks(size_type i, ranks_type& results) const
{
  if(i > this->size()) { i = this->size(); }

  size_type block = this->block_rank(i);
  for(size_type c = 1; c < SIGMA; c++) { results[c] = this->samples[c].sum(block); }
  size_type rle_pos = block * SAMPLE_RATE;
  size_type seq_pos = (block > 0 ? this->block_select(block) + 1 : 0);

  size_type prev = 0;
  while(seq_pos < i)
  {
    range_type run = Run::read(this->data, rle_pos);
    seq_pos += run.second;  // The starting position of the next run.
    results[run.first] += run.second; prev = run.first;
  }
  results[prev] -= seq_pos - i;
}

void
BWT::ranks(range_type range, rank_ranges_type& results) const
{
  range.first = std::min(range.first, this->size() - 1);
  range.second = std::min(range.second, this->size() - 1);
  for(size_type c = 1; c < SIGMA; c++) { results[c] = range_type(0, 0); }

  size_type block = this->block_rank(range.first);
  size_type rle_pos = block * SAMPLE_RATE;
  size_type seq_pos = (block > 0 ? this->block_select(block) + 1 : 0);

  // Compute the ranks within the block until range.first.
  range_type run(0, 0);
  while(seq_pos < range.first)
  {
    run = Run::read(this->data, rle_pos);
    seq_pos += run.second;  // The starting position of the next run.
    results[run.first].first += run.second;
    results[run.first].second += run.second;
  }
  results[run.first].first -= seq_pos - range.first;

  // Process the actual range.
  while(seq_pos <= range.second)
  {
    run = Run::read(this->data, rle_pos);
    seq_pos += run.second; // The starting position of the next run.
    results[run.first].second += run.second;
  }
  results[run.first].second -= (seq_pos - 1) - range.second;

  // Add the ranks before the block if there were occurrences.
  for(size_type c = 1; c < SIGMA; c++)
  {
    if(results[c].second > results[c].first)
    {
      size_type temp = this->samples[c].sum(block);
      results[c].first += temp; results[c].second += temp;
    }
  }
}

size_type
BWT::select(size_type i, comp_type c) const
{
  if(c >= SIGMA) { return 0; }
  if(i == 0) { return 0; }
  if(i > this->count(c)) { return this->size(); }

  size_type block = this->samples[c].inverse(i - 1);
  size_type count = this->samples[c].sum(block);
  size_type rle_pos = block * SAMPLE_RATE;
  size_type seq_pos = (block > 0 ? this->block_select(block) + 1 : 0);
  while(true)
  {
    range_type run = Run::read(this->data, rle_pos);
    seq_pos += run.second - 1;  // The last position in the run.
    if(run.first == c)
    {
      count += run.second;  // Number of c's up to the end of the run.
      if(count >= i) { return seq_pos + i - count; }
    }
    seq_pos++;  // Move to the first position in the next run.
  }
}

comp_type
BWT::operator[](size_type i) const
{
  if(i >= this->size()) { return 0; }

  size_type block = this->block_rank(i);
  size_type rle_pos = block * SAMPLE_RATE;
  size_type seq_pos = (block > 0 ? this->block_select(block) + 1 : 0);
  while(true)
  {
    range_type run = Run::read(this->data, rle_pos);
    seq_pos += run.second;  // The start of the next run.
    if(seq_pos > i) { return run.first; }
  }
}

range_type
BWT::inverse_select(size_type i) const
{
  range_type run(0, 0);
  if(i >= this->size()) { return run; }

  size_type block = this->block_rank(i);
  size_type rle_pos = block * SAMPLE_RATE;
  size_type seq_pos = (block > 0 ? this->block_select(block) + 1 : 0);

  size_type ranks[SIGMA] = {};
  while(seq_pos <= i)
  {
    run = Run::read(this->data, rle_pos);
    seq_pos += run.second;  // The starting position of the next run.
    ranks[run.first] += run.second; // Number of c's before the next run.
  }

  return range_type(this->samples[run.first].sum(block) + ranks[run.first] - (seq_pos - i), run.first);
}

//------------------------------------------------------------------------------

void
BWT::setHeader(const sdsl::int_vector<64>& counts)
{
  this->header.sequences = counts[0];
  this->header.bases = 0;
  for(size_type c = 0; c < counts.size(); c++) { this->header.bases += counts[c]; }
}

void
BWT::build(const sdsl::int_vector<64>& counts)
{
  size_type blocks = (this->bytes() + SAMPLE_RATE - 1) / SAMPLE_RATE;
  sdsl::sd_vector_builder block_ends(this->size(), blocks);
  sdsl::sd_vector_builder block_counts[SIGMA];
  for(size_type c = 0; c < SIGMA; c++)
  {
    block_counts[c] = sdsl::sd_vector_builder(counts[c] + blocks, blocks);
  }

  // Scan the BWT and determine block boundaries and ranks.
  size_type seq_pos = 0, rle_pos = 0;
  sdsl::int_vector<64> cumulative(SIGMA, 0);
  while(rle_pos < this->bytes())
  {
    range_type run = Run::read(this->data, rle_pos);
    seq_pos += run.second; cumulative[run.first] += run.second;
    if(rle_pos >= this->bytes() || rle_pos % SAMPLE_RATE == 0)
    {
      block_ends.set(seq_pos - 1);
      for(size_type c = 0; c < SIGMA; c++)
      {
        block_counts[c].set(cumulative[c]); cumulative[c]++;
      }
    }
  }

  // Build rank/select support.
  this->block_boundaries = sdsl::sd_vector<>(block_ends);
  sdsl::util::init_support(this->block_rank, &(this->block_boundaries));
  sdsl::util::init_support(this->block_select, &(this->block_boundaries));
  for(size_type c = 0; c < SIGMA; c++)
  {
    this->samples[c] = CumulativeArray(block_counts[c]);
  }
}

void
BWT::destroy()
{
  for(size_type c = 0; c < SIGMA; c++) { sdsl::util::clear(this->samples[c]); }
  sdsl::util::clear(this->block_boundaries);
  sdsl::util::clear(this->block_rank);
  sdsl::util::clear(this->block_select);
}

//------------------------------------------------------------------------------

void
BWT::characterCounts(sdsl::int_vector<64>& counts)
{
  counts = sdsl::int_vector<64>(SIGMA, 0);

  size_type rle_pos = 0, seq_pos = 0;
  while(rle_pos < this->bytes())
  {
    range_type run = Run::read(this->data, rle_pos);
    counts[run.first] += run.second; seq_pos += run.second;
  }
}

size_type
BWT::hash() const
{
  size_type res = FNV_OFFSET_BASIS;
  size_type rle_pos = 0;
  while(rle_pos < this->bytes())
  {
    range_type run = Run::read(this->data, rle_pos);
    for(size_type i = 0; i < run.second; i++) { res = fnv1a_hash((byte_type)(run.first), res); }
  }
  return res;
}

//------------------------------------------------------------------------------

} // namespace bwtmerge
