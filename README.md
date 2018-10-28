# Symbolic Liveness Analysis of Real-World Software

In Symbolic Liveness Analysis, we extended KLEE, a popular Symbolic Execution engine, for the detection of liveness violations.
To this end, we defined a generic and practically useful livenss violation for real-world software:

> A program is live iff it always eventually consumes input or terminates. [1]

This liveness property allows us to detect infinite loop bugs, while excluding intentional infinite loops, such as event loops in GUI or server applications.

For detection, we constructed a fingerprinting scheme that allows us to efficiently compute and update fingerprints of symbolic execution states, which can then be compared against each other.

Additional information on our technique can be found in our paper [1], as well as an evaluation of its runtime and memory overhead.

## Bugs Found

During our evaluation on GNU Coreutils, BusyBox, toybox and GNU sed, we found a total of five previously undiscovered software defects:

  * GNU Coreutils, tail #1: [https://debbugs.gnu.org/24495](https://debbugs.gnu.org/24495)
  * GNU Coreutils, tail #2: [https://debbugs.gnu.org/24903](https://debbugs.gnu.org/24903)
  * GNU Coreutils, ptx: [https://debbugs.gnu.org/28417](https://debbugs.gnu.org/28417)
  * BusyBox, hush #1: [https://bugs.busybox.net/10421](https://bugs.busybox.net/10421)
  * BusyBox, hush #2: [https://bugs.busybox.net/10686](https://bugs.busybox.net/10686)

In addition, we identified a bug in the long decommissioned GNU regex, which is still used in klee-uclibc by default:

  * GNU regex: [http://lists.gnu.org/archive/html/bug-gnu-utils/2018-04/msg00006.html](http://lists.gnu.org/archive/html/bug-gnu-utils/2018-04/msg00006.html)

## Requirements

We recommend to compile our version of KLEE against **LLVM 3.8.1**. For information on how to compile KLEE in general, please refer to README-CMake.md.

In addition to the usual requirements of KLEE, we use CryptoPP (Version 7.0.0) to generate BLAKE2b hashes (for fingerprint creation).

If CryptoPP is located at a custom location on your system, you can use the following to tell CMake where to find it:

```
cmake [...] -DCRYPTOPP_INCLUDE_DIRS="/path/to/CryptoPP/include" -DCRYPTOPP_LIBRARIES="/path/to/CryptoPP/libcryptopp.so"
```

In addition, make sure that KLEE can find `libcryptopp.so` in the library path at runtime.

## Usage

To use our analysis, we added the following options to KLEE:

```
-detect-infinite-loops
```
Enable detection of infinite loops (default=false)

```
-debug-infinite-loop-detection={state:stderr,trace:stderr}
```
Log all MemoryState / MemoryTrace information to stderr

```
-infinite-loop-detection-truncate-on-fork
```
Truncate memory trace (used for infinite loop detection) on every state fork (default=true)

```
-infinite-loop-detection-disable-two-predecessor-optimization
```
Disable infinite loop detection optimzation that only starts searching for loops on basic blocks with at least two predecessors (default=false)

## CAV 2018 Artifact Evaluation

Along with submission to CAV 2018, we prepared an artifact for evaluation, which can be obtained through the following links:

  * [Instructions](https://www.dropbox.com/s/xzg8vebwrgaic73/cav18-SymbolicLivenessAnalysis.txt?dl=0)
  * [VM image (*.ova)](https://www.dropbox.com/s/q01nh5fvin79l0w/cav18-SymbolicLivenessAnalysis.ova?dl=0)

## Publication

If you use any portion of Symbolic Liveness Analysis in your work, please cite the following paper:

[1] [Daniel Schemmel, Julian Büning, Oscar Soria Dustmann, Thomas Noll and Klaus Wehrle. *Symbolic Liveness Analysis of Real-World Software*. In Proceedings of the 30th International Conference on Computer Aided Verification (CAV'18). Lecture Notes in Computer Science, vol 10982. Springer, Cham](https://link.springer.com/chapter/10.1007/978-3-319-96142-2_27)

BibTeX:
```
@inproceedings {SymbolicLivenessAnalysis,
   author = {Schemmel, Daniel and B{\"{u}}ning, Julian and Soria Dustmann, Oscar and Noll, Thomas and Wehrle, Klaus},
   title = {{Symbolic Liveness Analysis of Real-World Software}},
   booktitle = {Proceedings of the 30th International Conference on Computer Aided Verification (CAV'18)},
   pages = {447--466},
   series = {Lecture Notes in Computer Science},
   volume = {10982},
   publisher = {Springer},
   address = {Cham},
   year = {2018}
}
```
