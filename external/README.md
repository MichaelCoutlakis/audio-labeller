# external/ External / third-party libraries

Every dependency except OpenGL, as a git submodule, pinned to a release:

| Folder     | Library                        |
| ---------- | ------------------------------ |
| `SDL`      | SDL3                           |
| `imgui`    | Dear ImGui (docking branch)    |
| `implot`   | ImPlot                         |
| `json`     | nlohmann/json                  |
| `spdlog`   | spdlog, with its bundled fmt   |
| `dr_libs`  | dr_wav, dr_flac, dr_mp3        |
| `doctest`  | doctest, for the tests         |

They're built with the app by default, so a clone builds without a package
manager. Configure with `-DAUDIOLAB_SYSTEM_DEPS=ON` to use installed packages
instead (eg. from vcpkg); Dear ImGui and ImPlot always come from here.

To move a library to another release, check it out in its folder and commit
the submodule:

```bash
git -C external/spdlog fetch --depth 1 origin tag v1.17.0
git -C external/spdlog checkout v1.17.0
git add external/spdlog
```
