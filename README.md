# Microsim

Microsim is a simplified version of MANTA, Microsimulation Analysis for Network Traffic Assignment (https://github.com/UDST/manta). MANTA employs a highly parallelized GPU implementation that is fast enough to run simulations on large-scale demand and networks within a few minutes - metropolitan and regional scale with hundreds of thousands of nodes and edges and millions of trips. We test our platform to simulate the entire Bay Area metropolitan region over the course of the morning using half-second time steps. The runtime for the nine-county Bay Area simulation is just over four minutes, not including routing and initialization. This computational performance significantly improves state of the art in large-scale traffic microsimulation and offers new capacity for analyzing the detailed travel patterns and travel choices of individuals for infrastructure planning and emergency management.

![](https://github.com/UDST/manta/blob/main/bay_bridge_trips.png)

## Dependencies

 - Boost 1.59
 - OpenCV (used versions: 3.2.0 in Ubuntu)
 - CUDA (used versions: 9.2 in Ubuntu)
 - g++ (used versions: 6.4.0 in Ubuntu)
 - Qt5 (used versions: 5.9.5 in Ubuntu)
 - qmake (used versions: 3.1 in Ubuntu)
 - Python (used versions: 3.6.5 in Ubuntu)
 - pytest (used versions: 6.1.1 in Ubuntu) 
 - pytest-cov (used versions: 2.10.1 in Ubuntu) 
 - pytest-remotedata (used versions: 0.3.2 in Ubuntu) 
 - psutil (used versions: 5.7.2 in Ubuntu) 
 - xlwt (used versions: 1.3.0 in Ubuntu)


## Installation & Compilation

Once the necessary dependencies are installed, add CUDA lib path to system paths:
```bash
export PATH=/usr/local/cuda-9.2/bin:$PATH
export LIBRARY_PATH=/usr/local/cuda-9.2/lib64:$LIBRARY_PATH 
export LD_LIBRARY_PATH=/usr/local/cuda-9.2/lib64:$LD_LIBRARY_PATH 
```

You can also add the `export` lines at the end of your user's `~/.bashrc` to
avoid re-entering them in each session.

Clone the repo in your home directory with:
```bash
git clone https://github.com/cb-cities/microsim.git ~/microsim && cd ~/microsim
```
Create Makefile and compile with:
```bash
sudo qmake LivingCity/LivingCity.pro && sudo make -j
```

## Data

Before running everything, you need the appropriate data:

1. Network
2. Demand

The networks currently reside in `manta/LivingCity/berkeley_2018`, and the default directory is the full SF Bay Area network in `new_full_network/`. This contains the `nodes.csv` and `edges.csv` files to create the network.

The demand is not in `new_full_network/`, but needs to reside there in order to run it. Please contact [Pavan Yedavalli](pavyedav@gmail.com) to procure real or sample demands.

## Running

If you wish to edit the microsimulation configuration, modify `manta/LivingCity/command_line_options.ini`, which contains the following:

```[General]
GUI=false
USE_CPU=false
NETWORK_PATH=berkeley_2018/new_full_network/
USE_JOHNSON_ROUTING=false
USE_SP_ROUTING=true
USE_PREV_PATHS=true
LIMIT_NUM_PEOPLE=256000
ADD_RANDOM_PEOPLE=false
NUM_PASSES=1
TIME_STEP=0.5
START_HR=5
END_HR=12
```

Here, you can modify the:

1. `GUI` - deprecated. Do not touch.
2. `USE_CPU` - deprecated. Do not touch.
3. `NETWORK_PATH` - specific path to the network files. Default is `berkeley_2018/new_full_network/`.
4. `USE_JOHNSON_ROUTING` - uses Johnson's all pairs shortest path routing. This should always be set to `false`.
5. `USE_SP_ROUTING` - uses new SP routing framework. This should always be set to `true`.
6. `USE_PREV_PATHS` - uses paths already produced and saved to file. Set to `false` if running for the first time. Set to `true` if the simulation was already run and it was saved to file. 
7. `LIMIT_NUM_PEOPLE` - deprecated. Do not touch.
8. `ADD_RANDOM_PEOPLE` - deprecated. Do not touch.
9. `NUM_PASSES` - the number of times the simulation is run. Set to 1.
10. `TIME_STEP` - timestep. Default is .5 seconds.
11. `START_HR` - start hour of the simulation. Default is 5am.
12. `END_HR` - end hour of the simulation. Default is 12pm.

Run with:
```bash
cd LivingCity
./LivingCity
```

## Development

Should you wish to make any changes, please create a new branch. In addition, once the original Makefile is created, you can simply run `sudo make -j` from the `manta/` directory to compile any new changes.

If necessary, you can checkout a different existing branch from main (`edge_speeds_over_time`, for instance):
```bash
git checkout edge_speeds_over_time
```

### Debugging
For debugging we recommend `cuda-gdb`. In order to use it, `manta/Makefile` must be modified by adding the flag `-G` to enable debugging and changing `-O3` to `-O` to avoid optimizations that restrict the use of the debugger.

For example, to enable debugging at `LivingCity/traffic/b18CUDA_trafficSimulator.cu`,  its compilation at the line `manta/Makefile:1756`:
<pre>
/usr/local/cuda-9.0/bin/nvcc -m64 <b>-O3</b> -arch=sm_50 -c --compiler-options -f
no-strict-aliasing -use_fast_math --ptxas-options=-v -Xcompiler -fopenmp -I/u
sr/include/opencv2/ -I/opt/local/include/ -I/usr/local/boost_1_59_0/ -I/home/
<b>{YOUR_USERNAME}</b>/manta/LivingCity/glew/include/ -I/usr/local/cuda-9.0/include  -L/opt/l
ocal/lib -lopencv_imgcodecs -lopencv_core -lopencv_imgproc -lcudart -lcuda -g -lgomp
LivingCity/traffic/b18CUDA_trafficSimulator.cu -o
${OBJECTS_DIR}b18CUDA_trafficSimulator_cuda.o
</pre>

must be modified to:
<pre>
/usr/local/cuda-9.0/bin/nvcc -m64 <b>-O</b> -arch=sm_50 -c --compiler-options -f
no-strict-aliasing -use_fast_math --ptxas-options=-v -Xcompiler -fopenmp -I/u
sr/include/opencv2/ -I/opt/local/include/ -I/usr/local/boost_1_59_0/ -I/home/
<b>{YOUR_USERNAME}</b>/manta/LivingCity/glew/include/ -I/usr/local/cuda-9.0/include  -L/opt/l
ocal/lib -lopencv_imgcodecs -lopencv_core -lopencv_imgproc -lcudart -lcuda -g <b>-G</b>
-lgomp LivingCity/traffic/b18CUDA_trafficSimulator.cu -o
${OBJECTS_DIR}b18CUDA_trafficSimulator_cuda.o
</pre>

After this modification, `sudo make clean` and `sudo make -j` must be run.

Please keep in mind that this alteration slows the program down. For more information about `cuda-gdb`, please refer to the official [Website](https://docs.nvidia.com/cuda/cuda-gdb/index.html) and [Documentation](https://www.google.com/url?sa=t&rct=j&q=&esrc=s&source=web&cd=&ved=2ahUKEwiBgbqg9fzrAhUMIrkGHby9Db8QFjADegQIAxAB&url=https%3A%2F%2Fdeveloper.download.nvidia.com%2Fcompute%2FDevZone%2Fdocs%2Fhtml%2FC%2Fdoc%2Fcuda-gdb.pdf&usg=AOvVaw3J9Il2vHkkxtcX83EHC3-z).

### Testing
In order to run the system tests you should first move to `manta/LivingCity`
```bash
cd LivingCity
```
and then run 
```bash
pytest -s -x tests/systemTestSuite.py
```
Because of the tests' long duration, we recommend using the flag `-s` to show the whole output of the simulation and `-x` to stop at the first failure.

### Benchmarking / profiling
In order to obtain a profiling of each component of the simulation, you should run
```bash
python3 LivingCity/benchmarking/runBenchmarks.py
```

If you wish to specify the name of the benchmark outputs and/or the number of iterations, just run:
```bash
python3 LivingCity/benchmarking/runBenchmarks.py --name={name_of_benchmark} --runs={number_of_iterations_to_run}
```
The script will run LivingCity the specified number of times while polling the system resources. For each component, its resource and time consumption will be saved into a `csv` file, a plot and a `xls` file in `manta/LivingCity/benchmarking/`. The profiling of each version is encouraged to be stored in [here](https://docs.google.com/spreadsheets/d/14KCUY8vLp9HoLuelYC5DmZwKI7aLsiaNFp7e6Z8bVBU/edit?usp=sharing).

Versions correspond to [the repository's tags](https://github.com/UDST/manta/tags). In order to create a new tag, just run
```bash
git tag v0.x.0
git push --tags
```


## Acknowledgments

This repository and code have been developed and maintained by Pavan Yedavalli, Ignacio Garcia Dorado, Krishna Kumar, and Paul Waddell. This work heavily derives from Ignacio Garcia Dorado's [Automatic Urban Modeling project](http://www.ignaciogarciadorado.com/p/2014_EG/2014_EG.html).

If this code is used in any shape or form for your project, please cite this paper accordingly:

P. Yedavalli, K. Kumar, and P. Waddell, “Microsimulation Analysis for Network Traffic Assignment (MANTA) at Metropolitan-Scale for Agile Transportation Planning,” arXiv:2007.03614 [physics], Jul. 2020, Available: http://arxiv.org/abs/2007.03614.

Thank you!




