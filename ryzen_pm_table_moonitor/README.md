# ryzen_pm_table_moonitor

This is a tool to monitor the power management table of AMD Ryzen processors on Linux systems. It provides a graphical interface to visualize various parameters and metrics related to the processor's power management.


## Cloning the Repository with Submodules

To clone this repository along with its submodules (imgui, glfw, taskflow, and implot), use the following command:

```sh
git clone --recurse-submodules https://github.com/plops/ryzen_managment_linux.git
```

If you have already cloned the repository without submodules, you can initialize and update them with:

```sh
git submodule update --init --recursive
```

The submodules will be checked out in the `extern/` directory:
- `extern/imgui`
- `extern/glfw`
- `extern/taskflow`
- `extern/implot`
