# Builds the engine with nixpkgs' own space-cadet-pinball derivation:
#
#   nix-build build.nix --no-out-link --out-link /tmp/scp-out
#
# nixpkgs knows the SDL2 layout the upstream CMake does not; configuring by hand
# against a /nix/store SDL2 fails inside its vendored FindSDL2.cmake.
#
# The source is the repo root rather than reference/, because CMakeLists.txt names
# the rule files as ../src/*.cpp and those have to be inside the copied tree for the
# path to resolve. sourceRoot then points the build back at reference/.
{pkgs ? import <nixpkgs> {}}:
pkgs.space-cadet-pinball.overrideAttrs (old: {
  pname = "space-cadet-pinball-fulltilt";
  version = "dev";

  src = builtins.path {
    path = ./.;
    name = "fulltilt-src";
    # Only what the build reads. Without this every rebuild re-copies the binaries,
    # the CD-sized game/ tree and the decompiled corpora -- gigabytes, and a changed
    # hash on every analysis run.
    filter = path: _type: let
      rel = pkgs.lib.removePrefix (toString ./. + "/") (toString path);
      top = builtins.head (pkgs.lib.splitString "/" rel);
    in
      builtins.elem top ["reference" "src"];
  };

  sourceRoot = "fulltilt-src/reference";
})
