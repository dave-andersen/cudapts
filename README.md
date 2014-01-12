CudaPTS - a GPU miner for Protoshares
==================================


This is a protoshares pool miner that runs on Nvidia GPUs.
It should run on any relatively modern card with 1GB of video RAM
or more.

This code is derived
from ptsminer, which is in turn
based on xolokrams's [primecoin miner](https://github.com/thbaumbach/primecoin).
and Invictus Innovations [protoshares client](https://github.com/InvictusInnovations/ProtoShares).
and jh00's & testix' [jhProtominer](https://github.com/jh000/jhProtominer).

It is hardcoded to work with beeeeer because that's the source I started
with.

To run, run:

```
    cudapts <payment-address> [cudaDevice] [shamode]
```

For most systems, it should work to simply run:

```
    cudapts <payment-address> 0
```

But if you have multiple GPUs or it doesn't work, you might have to
specify cudaDevice or shamode.

You should expect to see anywhere from 200 c/m up to over 1800c/m on
high-end dual-core devices.

Build notes:
You must install:
 - libboost
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

If you use an older card, you must edit makefile.(platform) and change
the architecture ("arch") specification from sm_30 to whatever is
appropriate for your card.  Look up your card on this page:
https://developer.nvidia.com/cuda-gpus
and replace "30" with whatever is listed for your card ("2.1" -> "sm_21",
for example).

Donations appreciated and will help convince the developer to make the
software even faster and easier to use.  grin.
  PTS:  Pr8cnhz5eDsUegBZD4VZmGDARcKaozWbBc
  BTC:  17sb5mcCnnt4xH3eEkVi6kHvhzQRjPRBtS
 
