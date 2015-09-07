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

#include "formats.h"

using namespace bwtmerge;

//------------------------------------------------------------------------------

int
main(int argc, char** argv)
{
  if(argc < 2)
  {
    std::cerr << "Usage: sga_inspect input1 [input2 ...]" << std::endl;
    std::cerr << std::endl;
    std::exit(EXIT_SUCCESS);
  }

  std::cout << "Inspecting BWT files in SGA format" << std::endl;
  std::cout << std::endl;

  SGAHeader total;
  for(int arg = 1; arg < argc; arg++)
  {
    std::ifstream in(argv[arg], std::ios_base::binary);
    if(!in)
    {
      std::cerr << "sga_inspect: Cannot open input file " << argv[arg] << std::endl;
      continue;
    }
    SGAHeader header(in); in.close();

    total.reads += header.reads; total.bases += header.bases; total.runs += header.runs;
    std::cout << argv[arg] << ": " << header << std::endl;
  }
  std::cout << std::endl;

  std::cout << "Total: " << total << std::endl;
  std::cout << std::endl;

  return 0;
}

//------------------------------------------------------------------------------