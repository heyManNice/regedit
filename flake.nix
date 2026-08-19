{
  description = "linux-regedit - 像 Windows regedit 一样浏览 Linux 系统配置";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs";
  };

  outputs = { self, nixpkgs }:
    let
      forAllSystems = nixpkgs.lib.genAttrs nixpkgs.lib.systems.flakeExposed;
    in
    {
      packages = forAllSystems (system:
        let
          pkgs = import nixpkgs { inherit system; };
        in
        {
          default = pkgs.stdenv.mkDerivation {
            pname = "linux-regedit";
            version = "0.1.0";

            src = self;

            nativeBuildInputs = with pkgs; [
              meson
              ninja
              pkg-config
            ];

            buildInputs = with pkgs; [
              gtk3
              glib
              json-glib
            ];

            mesonFlags = [
              "-Dtests=false"
            ];

            meta = with pkgs.lib; {
              description = "Linux 版的 regedit —— 系统配置文件浏览器";
              homepage = "https://github.com/heyManNice/regedit";
              license = licenses.gpl3Plus;
              platforms = platforms.linux;
              mainProgram = "linux-regedit";
            };
          };
        });

      apps = forAllSystems (system: {
        default = {
          type = "app";
          program = "${self.packages.${system}.default}/bin/linux-regedit";
        };
      });
    };
}