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

#include <cstring>

#include "formats.h"

namespace bwtmerge
{

//------------------------------------------------------------------------------

Alphabet
createAlphabet(AlphabeticOrder order)
{
  Alphabet alpha;

  switch(order)
  {
  case AO_DEFAULT:
    break;
  case AO_SORTED:
    std::swap(alpha.comp2char[4], alpha.comp2char[5]);
    std::swap(alpha.char2comp['N'], alpha.char2comp['T']);
    std::swap(alpha.char2comp['n'], alpha.char2comp['t']);
    break;
  default:
    break;
  }

  return alpha;
}

AlphabeticOrder
identifyAlphabet(const Alphabet& alpha)
{
  if(alpha.sorted()) { return AO_SORTED; }

  Alphabet default_alpha;
  if(alpha == default_alpha) { return AO_DEFAULT; }

  return AO_UNKNOWN;
}

std::string
alphabetName(AlphabeticOrder order)
{
  switch(order)
  {
  case AO_DEFAULT:
    return "default";
  case AO_SORTED:
    return "sorted";
  case AO_ANY:
    return "any";
  case AO_UNKNOWN:
  default:
    return "unknown";
  }
}

bool
compatible(const Alphabet& alpha, AlphabeticOrder order)
{
  Alphabet default_alpha;
  switch(order)
  {
  case AO_DEFAULT:
    return (alpha == default_alpha);
  case AO_SORTED:
    return alpha.sorted();
  case AO_ANY:
    return true;
  case AO_UNKNOWN:
  default:
    return false;
  }
}

//------------------------------------------------------------------------------

const std::string NativeFormat::name = "Native format";
const std::string NativeFormat::tag = "native";

//------------------------------------------------------------------------------

template<> const std::string PlainFormat<AO_DEFAULT>::name = "Plain format (default)";
template<> const std::string PlainFormat<AO_SORTED>::name = "Plain format (sorted)";
template<> const std::string PlainFormat<AO_DEFAULT>::tag = "plain_default";
template<> const std::string PlainFormat<AO_SORTED>::tag = "plain_sorted";

struct PlainData
{
  typedef uint8_t code_type;
};

void
readPlain(std::istream& in, BlockArray& data, sdsl::int_vector<64>& counts, const Alphabet& alpha)
{
  data.clear();
  counts = sdsl::int_vector<64>(alpha.sigma, 0);

  RunBuffer run_buffer;
  std::vector<PlainData::code_type> buffer(MEGABYTE);
  while(true)
  {
    in.read((char*)(buffer.data()), buffer.size());
    size_type bytes = in.gcount();
    if(bytes == 0) { break; }
    for(size_type i = 0; i < bytes; i++)
    {
      if(run_buffer.add(buffer[i]))
      {
        run_buffer.run.first = alpha.char2comp[run_buffer.run.first];
        Run::write(data, run_buffer.run);
        counts[run_buffer.run.first] += run_buffer.run.second;
      }
    }
  }
  run_buffer.flush();
  run_buffer.run.first = alpha.char2comp[run_buffer.run.first];
  Run::write(data, run_buffer.run);
  counts[run_buffer.run.first] += run_buffer.run.second;
}

void
writePlain(std::ostream& out, const BlockArray& data, const Alphabet& alpha)
{
  size_type rle_pos = 0, buffer_pos = 0;
  std::vector<PlainData::code_type> buffer(MEGABYTE);
  while(rle_pos < data.size())
  {
    range_type run = Run::read(data, rle_pos);
    run.first = alpha.comp2char[run.first];
    while(run.second > 0)
    {
      if(buffer_pos >= buffer.size())
      {
        out.write((char*)(buffer.data()), buffer.size());
        buffer_pos = 0;
      }
      size_type length = std::min(buffer.size() - buffer_pos, run.second); run.second -= length;
      for(size_type i = 0; i < length; i++, buffer_pos++) { buffer[buffer_pos] = run.first; }
    }
  }
  if(buffer_pos > 0)
  {
    out.write((char*)(buffer.data()), buffer_pos);
  }
}

//------------------------------------------------------------------------------

struct SDSLData
{
  typedef uint64_t code_type;

  inline static size_type bits2values(size_type bits) { return (bits + VALUE_SIZE - 1) / VALUE_SIZE; }
  inline static size_type values2blocks(size_type values) { return (values + BLOCK_SIZE - 1) / BLOCK_SIZE; }

  const static size_type VALUE_SIZE = 8;
  const static size_type BLOCK_SIZE = 8;
  const static size_type SIGMA = 6;

  static void read(std::istream& in, BlockArray& data, sdsl::int_vector<64>& counts, const Alphabet& alpha);
  static void write(std::ostream& out, const BlockArray& data, const Alphabet& alpha);
};

void
SDSLData::read(std::istream& in, BlockArray& data, sdsl::int_vector<64>& counts, const Alphabet& alpha)
{
  data.clear();
  counts = sdsl::int_vector<64>(alpha.sigma, 0);

  // Header.
  size_type bits; sdsl::read_member(bits, in);
  size_type bytes = bits2values(bits);

  // Data.
  RunBuffer run_buffer;
  sdsl::int_vector<8> buffer(MEGABYTE);
  for(size_type offset = 0; offset < bytes; offset += buffer.size())
  {
    size_type buffer_size = std::min(buffer.size(), bytes - offset);
    in.read((char*)(buffer.data()), values2blocks(buffer_size) * sizeof(SDSLData::code_type));
    for(size_type i = 0; i < buffer_size; i++)
    {
      if(run_buffer.add(buffer[i]))
      {
        run_buffer.run.first = alpha.char2comp[run_buffer.run.first];
        Run::write(data, run_buffer.run);
        counts[run_buffer.run.first] += run_buffer.run.second;
      }
    }
  }
  run_buffer.flush();
  run_buffer.run.first = alpha.char2comp[run_buffer.run.first];
  Run::write(data, run_buffer.run);
  counts[run_buffer.run.first] += run_buffer.run.second;
}

void
SDSLData::write(std::ostream& out, const BlockArray& data, const Alphabet& alpha)
{
  // Header.
  size_type bits = 0, rle_pos = 0;
  while(rle_pos < data.size())
  {
    range_type run = Run::read(data, rle_pos);
    bits += run.second * VALUE_SIZE;
  }
  sdsl::write_member(bits, out);

  // Data.
  rle_pos = 0;
  size_type buffer_pos = 0;
  sdsl::int_vector<8> buffer(MEGABYTE);
  while(rle_pos < data.size())
  {
    range_type run = Run::read(data, rle_pos);
    run.first = alpha.comp2char[run.first];
    while(run.second > 0)
    {
      if(buffer_pos >= buffer.size())
      {
        out.write((char*)(buffer.data()), buffer.size());
        buffer_pos = 0;
      }
      size_type length = std::min(buffer.size() - buffer_pos, run.second); run.second -= length;
      for(size_type i = 0; i < length; i++, buffer_pos++) { buffer[buffer_pos] = run.first; }
    }
  }
  if(buffer_pos > 0)
  {
    size_type blocks = values2blocks(buffer_pos);
    out.write((char*)(buffer.data()), blocks * BLOCK_SIZE);
  }
}

//------------------------------------------------------------------------------

const std::string RFMFormat::name = "RFM format";
const std::string RFMFormat::tag = "rfm";

void
RFMFormat::read(std::istream& in, BlockArray& data, sdsl::int_vector<64>& counts)
{
  SDSLData::read(in, data, counts, Alphabet(SDSLData::SIGMA));
}

void
RFMFormat::write(std::ostream& out, const BlockArray& data)
{
  SDSLData::write(out, data, Alphabet(SDSLData::SIGMA));
}

//------------------------------------------------------------------------------

const std::string SDSLFormat::name = "SDSL format";
const std::string SDSLFormat::tag = "sdsl";

void
SDSLFormat::read(std::istream& in, BlockArray& data, sdsl::int_vector<64>& counts)
{
  SDSLData::read(in, data, counts, createAlphabet(order()));
}

void
SDSLFormat::write(std::ostream& out, const BlockArray& data)
{
  SDSLData::write(out, data, createAlphabet(order()));
}

//------------------------------------------------------------------------------

const std::string SGAFormat::name = "SGA format";
const std::string SGAFormat::tag = "sga";

struct SGAData
{
  typedef bwtmerge::comp_type comp_type;
  typedef bwtmerge::size_type length_type;
  typedef uint8_t             code_type;

  inline static code_type encode(comp_type comp, length_type length) { return (comp << RUN_BITS) | length; }
  inline static comp_type comp(code_type code) { return (code >> RUN_BITS); }
  inline static length_type length(code_type code) { return (code & RUN_MASK); }

  const static code_type RUN_MASK = 0x1F;
  const static size_type RUN_BITS = 5;
  const static size_type MAX_RUN = 31;
  const static size_type SIGMA = 6;
};

void
SGAFormat::read(std::istream& in, BlockArray& data, sdsl::int_vector<64>& counts)
{
  data.clear();
  counts = sdsl::int_vector<64>(SGAData::SIGMA, 0);

  SGAHeader header(in);
  if(!(header.check()))
  {
    std::cerr << "SGAFormat::load(): Invalid header!" << std::endl;
    std::exit(EXIT_FAILURE);
  }

  SGAData::code_type buffer[MEGABYTE];
  RunBuffer run_buffer;
  for(size_type offset = 0; offset < header.runs; offset += MEGABYTE)
  {
    size_type bytes = std::min(MEGABYTE, header.runs - offset);
    in.read((char*)buffer, bytes);
    for(size_type i = 0; i < bytes; i++)
    {
      if(run_buffer.add(SGAData::comp(buffer[i]), SGAData::length(buffer[i])))
      {
        Run::write(data, run_buffer.run);
        counts[run_buffer.run.first] += run_buffer.run.second;
      }
    }
  }
  run_buffer.flush();
  Run::write(data, run_buffer.run);
  counts[run_buffer.run.first] += run_buffer.run.second;
}

void
SGAFormat::write(std::ostream& out, const BlockArray& data)
{
  SGAHeader header;
  size_type rle_pos = 0;
  while(rle_pos < data.size())
  {
    range_type run = Run::read(data, rle_pos);
    if(run.first == 0) { header.sequences += run.second; }
    header.bases += run.second;
    header.runs += (run.second + SGAData::MAX_RUN - 1) / SGAData::MAX_RUN;
  }
  header.write(out);

  rle_pos = 0;
  std::vector<SGAData::code_type> buffer;
  while(rle_pos < data.size())
  {
    range_type run = Run::read(data, rle_pos);
    while(run.second > SGAData::MAX_RUN)
    {
      buffer.push_back(SGAData::encode(run.first, SGAData::MAX_RUN));
      run.second -= SGAData::MAX_RUN;
    }
    buffer.push_back(SGAData::encode(run.first, run.second));
    if(buffer.size() >= MEGABYTE)
    {
      out.write((char*)(buffer.data()), buffer.size());
      buffer.clear();
    }
  }
  out.write((char*)(buffer.data()), buffer.size());
}

//------------------------------------------------------------------------------

NativeHeader::NativeHeader() :
  tag(DEFAULT_TAG), flags(0), sequences(0), bases(0)
{
}

NativeHeader::NativeHeader(std::istream& in)
{
  sdsl::read_member(this->tag, in);
  sdsl::read_member(this->flags, in);
  sdsl::read_member(this->sequences, in);
  sdsl::read_member(this->bases, in);
}

void
NativeHeader::write(std::ostream& out) const
{
  sdsl::write_member(this->tag, out);
  sdsl::write_member(this->flags, out);
  sdsl::write_member(this->sequences, out);
  sdsl::write_member(this->bases, out);
}

bool
NativeHeader::check() const
{
  return (this->tag == DEFAULT_TAG);
}

AlphabeticOrder
NativeHeader::order() const
{
  return static_cast<AlphabeticOrder>((this->flags) & ALPHABET_MASK);
}

void
NativeHeader::setOrder(AlphabeticOrder ao)
{
  this->flags &= ~ALPHABET_MASK;
  this->flags |= static_cast<uint32_t>(ao) & ALPHABET_MASK;
}

std::ostream& operator<<(std::ostream& stream, const NativeHeader& header)
{
  return stream << "Native format: " << header.sequences << " sequences, " << header.bases << " bases, "
                << alphabetName(header.order()) << " alphabet";
}

//------------------------------------------------------------------------------

SGAHeader::SGAHeader() :
  tag(DEFAULT_TAG), sequences(0), bases(0), runs(0), flag(DEFAULT_FLAG)
{
}

SGAHeader::SGAHeader(std::istream& in)
{
  sdsl::read_member(this->tag, in);
  sdsl::read_member(this->sequences, in);
  sdsl::read_member(this->bases, in);
  sdsl::read_member(this->runs, in);
  sdsl::read_member(this->flag, in);
}

void
SGAHeader::write(std::ostream& out) const
{
  sdsl::write_member(this->tag, out);
  sdsl::write_member(this->sequences, out);
  sdsl::write_member(this->bases, out);
  sdsl::write_member(this->runs, out);
  sdsl::write_member(this->flag, out);
}

bool
SGAHeader::check() const
{
  return (this->tag == DEFAULT_TAG && this->flag == DEFAULT_FLAG);
}

std::ostream& operator<<(std::ostream& stream, const SGAHeader& header)
{
  return stream << "SGA format: " << header.sequences << " sequences, "
                << header.bases << " bases, " << header.runs << " runs";
}

//------------------------------------------------------------------------------

} // namespace bwtmerge
