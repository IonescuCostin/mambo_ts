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
    

Advanced configuration
----------------------

MAMBO DRD achieves dynamic race detection through the help of vector clocks. The implementation closley follows the algorithms present in the "VerifiedFT: a verified, high-performance precise dynamic race detector" publication.
A more detailed description with links to resources is coming soon.
