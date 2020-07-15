# Symbolic Partial-Order Execution for Testing Multi-Threaded Programs

In POR-SE, we combine Quasi-Optimal Partial-Order Reduction (POR), a state-of-the-art POR that handles interleaving non-determinism, with Symbolic Execution (SE) to handle data non-determinism.
Our implementation is based on [KLEE](https://klee.github.io), a popular Symbolic Execution engine, which we extended to deal with multithreaded (pthread) programs.

For a more detailed description of our technique, please refer to [1].

## Building Instructions

We recommend building this tool using our [workspace](https://www.github.com/por-se/workspace/).

## Bugs Found

During our evaluation, we found the following previously undiscovered software defects:

  * 6 double-initialized mutexes in memcached - [Bug Report](https://github.com/memcached/memcached/pull/566)
  * Data race in memcached - [Bug Report](https://github.com/memcached/memcached/issues/567)
  * Data race #2 in memcached - [Bug Report](https://github.com/memcached/memcached/pull/573)
  * Data race #3 in memcached - [Bug Report](https://github.com/memcached/memcached/pull/575)

## Publication

If you use (or want to refer to) this tool in your own work, please cite the following paper:

[1] Daniel Schemmel, Julian Büning, César Rodríguez, David Laprell and Klaus Wehrle. *Symbolic Partial-Order Execution for Testing Multi-Threaded Programs*. In Proceedings of the 32nd International Conference on Computer Aided Verification (CAV'20), July 2020 (*accepted*, to appear)

In addition to the above paper we also present its extended version:

[2] Daniel Schemmel, Julian Büning, César Rodríguez, David Laprell and Klaus Wehrle. [*Symbolic Partial-Order Execution for Testing Multi-Threaded Programs*](https://arxiv.org/abs/2005.06688). [arXiv:2005.06688](https://arxiv.org/abs/2005.06688) [cs], May 2020.
