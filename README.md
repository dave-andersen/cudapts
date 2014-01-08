CudaPTS - a GPU miner for Protoshares
==================================


This is a protoshares pool miner that runs on Nvidia GPUs.
It has been confirmed to work on a lot of Kepler (GK110 and GK104)
devices, as well as at least one GTX 570 (compute capability 2).

This code is derived
from ptsminer, which is in turn
based on xolokrams's [primecoin miner](https://github.com/thbaumbach/primecoin).
and Invictus Innovations [protoshares client](https://github.com/InvictusInnovations/ProtoShares).
and jh00's & testix' [jhProtominer](https://github.com/jh000/jhProtominer).

Requires at least 800MB of free graphics memory.  It *will not run*
with less.

It is hardcoded to work with beeeeer because that's the source I started
with.

To run, run:
   cudapts <payment-address> [cudaDevice] [shamode]

For most systems, it should work to simply run:

   cudapts <payment-address>

But if you have multiple GPUs or it doesn't work, you might have to
specify cudaDevice or shamode.

You should expect to see anywhere from 200 c/m up to over 1000c/m on
high-end dual-core devices.

Build notes:
You must install:
 - libboost
 - yasm
 - nvcc and the nvidia libraries

You must have nvcc in your path, and the nvidia libraries in your
LD_LIBRARY_PATH when you run the code.  You may need to adjust the
Makefile for your platform to point to the CUDA directory.

Run with:
  make -f makefile.YOURSYSTEM

as in, for Linux,
  make -f makefile.unix

I don't know if it needs a specific CUDA revision, but I've only tested
with CUDA 5.5.
