# BWT-merge

This is a tool for merging the Burrows-Wheeler transforms (BWT) of large read collections. Querying the merged BWT is often faster than querying the BWT of each dataset separately. If the datasets are similar (e.g. reads from similar genomes), a run-length encoded merged BWT will usually be smaller than the run-length encoded BWTs of individual datasets.

If the dataset is up to a few hundred gigabytes in size, it is probably more practical to use another tool (see "Algorithms and implementations" below) to build the merged BWT directly.

## Usage

The implementation is based on the [Succinct Data Structures Library 2.0 (SDSL)](https://github.com/simongog/sdsl-lite). To compile, set `SDSL_DIR` in the Makefile to point to your SDSL directory. As the implementation uses C++11, OpenMP, and libstdc++ parallel mode, you need g++ 4.7 or newer to compile. Comment out the line `OUTPUT_FLAGS=-DVERBOSE_STATUS_INFO` if you do not want the merging tool to output status information to `stderr`.

There are three tools in the package:

`bwt_convert input output` reads a run-length encoded BWT built by the [String Graph Assembler](https://github.com/jts/sga) from file `input` and writes it to file `output` in the native format of BWT-merge. The converted file is often a bit smaller than the input, even though it includes rank/select indexes.

`bwt_inspect input1 [input2 ...]` tries to identify the BWT formats of the input files. If successful, it will also display some basic information about the files. Only the native format and the SGA format are currently supported.

`bwt_merge input1 input2 output [patterns]` reads the native format BWT files `input1` and `input2`, merges them, and writes the merged BWT to file `output` in the native format. The sequences from `input2` are inserted into `input1`, so `input2` should usually be the smaller of the two. If the optional parameter `patterns` is present, the merging is verified by querying the BWTs by patterns read from file `patterns` (one pattern per line).

## Current performance

The input consists of several BWT files from the ReadServer project. Each of the files contains unique (error-corrected, trimmed) reads ending with the bases in the file name.

|File         |     AA|     TT|     AT|     TA|
|-------------|:-----:|:-----:|:-----:|:-----:|
|Size         |438 Gbp|436 Gbp|278 Gbp|359 Gbp|
|Sequences    |  4.69G|  4.67G|  2.98G|  3.84G|
|SGA format   |39.9 GB|40.0 GB|26.9 GB|33.5 GB|
|SGA + index  |48.3 GB|48.4 GB|32.6 GB|40.6 GB|
|Native format|38.5 GB|38.7 GB|26.6 GB|32.7 GB|

|Files          |     AA|   AA + TT|AA + TT + AT|AA + TT + AT + TA|
|---------------|:-----:|:--------:|:----------:|:---------------:|
|Size           |438 Gbp|   874 Gbp|    1.15 Tbp|         1.51 Tbp|
|Sequences      |  4.69G|     9.37G|       12.3G|            16.2G|
|Native format  |38.5 GB|   71.3 GB|     91.5 GB|           117 GB|
|Time (merge)   |      –|    13.9 h|      10.6 h|           13.8 h|
|Speed (merge)  |      –|8.71 Mbp/s|  7.28 Mbp/s|       7.20 Mbp/s|
|Memory (merge) |      –|    152 GB|      178 GB|           208 GB|
|Disk (merge)   |      –|    268 GB|      220 GB|           289 GB|
|Time (total)   |      –|    13.9 h|      24.6 h|           38.4 h|
|Speed (average)|      –|8.71 Mbp/s|  8.08 Mbp/s|       7.77 Mbp/s|
|Memory (peak)  |      –|    152 GB|      178 GB|           208 GB|
|Disk (peak)    |      –|    268 GB|      268 GB|           289 GB|

The experiments were run on a system with two 16-core AMD Opteron 6378 processors and 256 gigabytes of memory. The measured times include disk I/O and index verification.

Some observations:
* Larger `input2` files increase merging speed, while larger `input1` files decrease it. (A larger `input2` file also means that there is more work to do.)
* Memory usage depends on the size of the BWTs, the number of threads, the number of merge buffers, and the size of buffers.
* Disk usage depends primarily on the size of `input2`, but also on the size of `input1`.

## Background

Building the BWT is a solved problem for sequence collections of up to hundreds of gigabytes in size. For larger collections, there are several issues to consider:

* **Construction time:** As a rough guideline, an algorithm indexing 1 MB/s is good for up to 100 gigabytes of data, and somewhat useful until 1 terabyte. Larger datasets require faster algorithms.
* **Construction space:** When the datasets are much larger than the amount of memory available, we cannot afford using even a single bit of working space per input character.
* **Available hardware:** In a typical computer cluster, a single node has two CPUs (with up to tens of cores), tens to hundreds of gigabytes of local memory, a limited amount of local disk space, and large amounts of shared (but often slow) disk space. Some tools require fast GPUs or large amounts of fast disk space, which are generally not available.
* **Resource usage:** Merging large BWTs is easy by doing a lot of redundant work on multiple nodes. Because computer clusters generally do not have large amounts of unused capacity, good tools should make an efficient use of resources.

## Algorithms and implementations

There are several BWT construction algorithms based on updating an existing BWT. Some algorithms **extend** sequences already in the collection, updating *BWT(T[i+1,j])* to *BWT(T[i,j])*. Others **insert** new sequences to the collection, updating *BWT(S)* to *BWT(S,T)*. In both cases, the algorithm can either do **batch updates** to a **static** BWT representation, or use a **dynamic** representation for the BWT. Algorithms with a static BWT representation require more working space, while algorithms with a dynamic representation have more space overhead for the BWT.

Some common implementations include:

* [BEETL](https://github.com/BEETL/BEETL): (extend, static) on disk
* [NVBIO](http://nvlabs.github.io/nvbio/): (insert, dynamic) using GPU
* [RLCSA](http://jltsiren.kapsi.fi/rlcsa): (insert, static)
* [ropebwt](https://github.com/lh3/ropebwt): (extend, static) or (insert by extending, dynamic)
* [ropebwt2](https://github.com/lh3/ropebwt2): (insert by extending, dynamic)

As this tool merges existing BWTs, it is (insert, static).

There are also other algorithms for building the BWT for large read collections They are based on partitioning the suffixes, sorting each of the partitions separately, and building the BWT directly.

## Version history

### Current version

* The native BWT format has a header.
* `sga_inspect` renamed to `bwt_inspect`. Now it also identifies files in the native format.

### Version 0.1

* The first pre-release.
* `sga_inspect` for inspecting BWT files in the SGA format.
* `bwt_convert` for converting BWT files from the SGA format to the native format.
* `bwt_merge` for merging BWT files in the native format.

## Future work

* Input/output in any supported BWT format.
* Different options for where the sequences from `input2` are inserted:
  * after the sequences from `input1` (current behavior)
  * in reverse lexicographic order
  * by position in the reference
* An option to remove duplicate sequences.
* Adjustable construction parameters:
  * number of threads and sequence blocks
  * size of thread-specific buffers
  * number of merge buffers
* Documentation in the wiki.

## References

Wing-Kai Hon, Tak-Wah Lam, Kunihiko Sadakane, Wing-Kin Sung, Siu-Ming Yiu:
**A space and time efficient algorithm for constructing compressed suffix arrays**.
Algorithmica 48(1): 23-36, 2007.
[DOI: 10.1007/s00453-006-1228-8](http://dx.doi.org/10.1007/s00453-006-1228-8)

Jouni Sirén: **Compressed Full-Text Indexes for Highly Repetitive Collections**.
PhD Thesis, University of Helsinki, June 2012.
[http://jltsiren.kapsi.fi/phd](http://jltsiren.kapsi.fi/phd)

Markus J. Bauer, Anthony J. Cox, and Giovanna Rosone:
**Lightweight algorithms for constructing and inverting the BWT of string collections**.
Theoretical Computer Science 483: 134-148, 2013.
[DOI: 10.1016/j.tcs.2012.02.002](http://dx.doi.org/10.1016/j.tcs.2012.02.002)
