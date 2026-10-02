# Agent instructions

Reference [README.md](README.md) for builds and library behavior,
[scripts/README.md](scripts/README.md) for the postprocessors, and
[FUZZING.md](FUZZING.md) for fuzzing.

* Run `./autogen.sh`, `./configure`, or `make distclean` only when explicitly
  requested by the user. The Autotools commands in the README are usage
  examples, not a request to regenerate this checkout.
* Format changed Python scripts with Black.
