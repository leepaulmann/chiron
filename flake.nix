{
  description = "Chiron: native Linux host for SMAC/SMACX using your own copy of the game";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      pkgs = nixpkgs.legacyPackages.x86_64-linux;
      # terranx.exe is 32-bit x86 code, so the host is an i686 ELF until the
      # original code is fully replaced.
      pkgs32 = pkgs.pkgsi686Linux;
    in {
      devShells.x86_64-linux.default = pkgs32.mkShell {
        nativeBuildInputs = [ pkgs.pkgsCross.mingw32.buildPackages.gcc pkgs.cmake pkgs.ninja pkgs.pkg-config pkgs.gdb pkgs.innoextract pkgs.python3 ];
        buildInputs = [ pkgs32.SDL2 pkgs32.freetype ];
      };
    };
}
