# Local build and installation

After building the game locally, always install the successful build to
`~/.local` using `cmake --install build-vulkan --prefix "$HOME/.local"`
(adjust the build directory if needed). The desktop shortcut launches
`~/.local/bin/the-perfect-v-vulkan`, which points to `the-perfect-v`
in the same directory. Updating only the executable in the build directory is
not a completed local build for this project.

Verify that the installed executable matches the built executable before
reporting completion. Keep existing saves intact.
