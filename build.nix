# Builds the reference tree with nixpkgs' own space-cadet-pinball derivation, so the
# port has a working baseline before any of src/ is wired in:
#
#   nix-build build.nix
#
# nixpkgs knows the SDL2 layout its upstream CMake does not; configuring by hand against
# a /nix/store SDL2 fails in FindSDL2.cmake.
{ pkgs ? import <nixpkgs> {} }:
pkgs.space-cadet-pinball.overrideAttrs (old: {
  pname = "space-cadet-pinball-fulltilt";
  src = ./reference;
  version = "dev";
})
