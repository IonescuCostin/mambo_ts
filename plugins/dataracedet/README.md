MAMBO Data Race Detector
==============

This instrumentation plugin for [MAMBO](https://github.com/beehive-lab/mambo) detects data races i.e. two or more threads in a single process access the same memory location concurrently.



Building:
---------

    git clone --recurse-submodules https://github.com/IonescuCostin/mambo_ts.git
    cd mambo
    make memcheck
    
    
Usage:
------

To run an application under MAMBO DRD, simply prefix the command with a call to `mambo_drd`. For example to execute `lscpu`, from the mambo source directory run:

    ./mambo_drd /usr/bin/lscpu
    
Example output from a buggy application:
---------------

    $ mambo_drd ~/test
    
    -- Coming soon --
    
More Details
----------------------

MAMBO DRD achieves dynamic race detection through the help of [vector clocks](https://en.wikipedia.org/wiki/Vector_clock). The implementation closley follows the algorithms present in the "[VerifiedFT: a verified, high-performance precise dynamic race detector](http://dept.cs.williams.edu/~freund/papers/18-ppopp.pdf)" publication. VerifiedFT represents an evolution of the [FastTrack](https://dl.acm.org/doi/10.1145/1543135.1542490) algorithm, however VerifiedFT maintains greater simplicity and provides a functional corectness guarantee. This project follows the **VerifiedFT-v1 Idealized Implementation** due to its simplicity, however greater efficiency can be achieved by implementing the **v2 Implementation** also present in the paper.


Testing
----------------------
A naive test suite is available at:
    
    mambo_ts/test_races/
    
To compile and run all tests you can execute the script:

    ./run_tests.sh
    
The test suite is not exhaustive and includes test cases which might not be relevant to the basic functionality of the plugin. This test suite requires further development. Inspiration for the tests was taken from the [ThreadSanitizer](https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual) project which is maintained by Google. The ThreadSanitizer test suite can be found [here](https://github.com/llvm/llvm-project/tree/e356027016c6365b3d8924f54c33e2c63d931492/compiler-rt/lib/tsan/tests).


Additional Resources
----------------------
 Some helpful youtube videos providing insight into the development of ThreadSanitizer can be found [here](https://youtu.be/5erqWdlhQLA) and [here](https://youtu.be/4r9Kr_HtGdI). These videos provide a broad insight into the concept of using vector clocks to detect data races.
