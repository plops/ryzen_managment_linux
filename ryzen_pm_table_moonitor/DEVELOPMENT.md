# Development Guide for ryzen_pm_table_moonitor

## Dependencies

This project relies on several external libraries, which are included as git submodules. The dependencies are:

- imgui (Immediate Mode Graphical User Interface)
- glfw (Graphics Library Framework)
- taskflow (Parallel Task Programming Library)
- implot (Immediate Mode Plotting Library)
- Boost (specifically Boost PFR for reflection)
- spdlog (Fast C++ Logging Library)


## Adding Submodules

To add the repositories (imgui, glfw, taskflow, and implot) as submodules to your repo, follow these steps:

Open a terminal and navigate to your project root directory:

```
cd /home/kiel/stage/ryzen_managment_linux/ryzen_pm_table_moonitor
```

Add each repository as a submodule (placing them in the extern/ directory):

```
git submodule add https://github.com/ocornut/imgui.git extern/imgui
git submodule add https://github.com/glfw/glfw.git extern/glfw
git submodule add https://github.com/taskflow/taskflow.git extern/taskflow
git submodule add https://github.com/epezent/implot.git extern/implot
git submodule add https://github.com/gabime/spdlog extern/spdlog
```

Stage and commit the changes:

```
git add .gitmodules extern/imgui extern/glfw extern/taskflow extern/implot
git commit -m "Add imgui, glfw, taskflow, and implot as git submodules"
```


To clone the repository with all submodules in the future, use:

```
git clone --recurse-submodules https://github.com/plops/ryzen_managment_linux.git
```

If you already cloned the repo, initialize and update submodules with:

```
git submodule update --init --recursive
```


## Updating Submodules to Newer Versions

To update any of the dependencies (imgui, glfw, taskflow, implot) to a newer version, follow these steps:

1. Fetch the latest changes for the submodule:
   ```sh
   cd extern/<submodule_name>
   git fetch
   ```

2. Checkout the desired commit, tag, or branch:
   ```sh
   git checkout <commit-or-tag>
   ```

3. Go back to the main repository root and record the new submodule state:
   ```sh
   cd ../..
   git add extern/<submodule_name>
   git commit -m "Update <submodule_name> to <commit-or-tag>"
   ```

4. Push the changes to your repository:
   ```sh
   git push
   ```

Repeat for each submodule you want to update.


## Boost

As I only need Boost PFR for this project (to display the table of the settings using introspection), I manually copied the necessary headers from Boost version 1.88.0-r1 into `src/boost`.

