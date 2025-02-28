// CUDA CODE
#include "assert.h"
#include "cuda.h"
#include "cuda_runtime.h"
#include "curand_kernel.h"
#include "device_launch_parameters.h"
#include <stdio.h>

#include "cuda_simulator.h"

#include <iostream>
#include <random>

#ifndef ushort
#define ushort uint16_t
#endif
#ifndef uint
#define uint uint32_t
#endif
#ifndef uchar
#define uchar uint8_t
#endif

///////////////////////////////
// CONSTANTS

__constant__ float intersectionClearance = 7.8f;

using namespace LC;
////////////////////////////////
// VARIABLES
LC::Agent *trafficPersonVec_d;
uint *indexPathVec_d;
LC::EdgeData *edgesData_d;
LC::IntersectionData *intersections_d;
uchar *laneMap_d;

__managed__ bool readFirstMapC = true;
__managed__ uint mapToReadShift;
__managed__ uint mapToWriteShift;
__managed__ int mutex = 0;
__managed__ uint halfLaneMap;

#define gpuErrchk(ans)                                                         \
  { gpuAssert((ans), __FILE__, __LINE__); }
inline void gpuAssert(cudaError_t code, const char *file, int line,
                      bool abort = true) {
  if (code != cudaSuccess) {
    fprintf(stderr, "GPUassert: %s %s %d\n", cudaGetErrorString(code), file,
            line);
    if (abort)
      exit(code);
  }
}
inline void printMemoryUsage() {
  // show memory usage of GPU
  size_t free_byte;
  size_t total_byte;
  cudaError_t cuda_status = cudaMemGetInfo(&free_byte, &total_byte);
  if (cudaSuccess != cuda_status) {
    printf("Error: cudaMemGetInfo fails, %s \n",
           cudaGetErrorString(cuda_status));
    exit(1);
  }
  double free_db = (double)free_byte;
  double total_db = (double)total_byte;
  double used_db = total_db - free_db;
  printf("GPU memory usage: used = %.0f, free = %.0f MB, total = %.0f MB\n",
         used_db / 1024.0 / 1024.0, free_db / 1024.0 / 1024.0,
         total_db / 1024.0 / 1024.0);
}

//! Allocate appropirate amount of memory on the cuda device
void init_cuda(bool fistInitialization, // create buffers
               std::vector<LC::Agent> &agents,
               std::vector<LC::EdgeData> &edgesData,
               std::vector<uchar> &laneMap,
               std::vector<LC::IntersectionData> &intersections) {

  { // agents
    size_t size = agents.size() * sizeof(LC::Agent);
    if (fistInitialization)
      gpuErrchk(cudaMalloc((void **)&trafficPersonVec_d,
                           size)); // Allocate array on device
    gpuErrchk(cudaMemcpy(trafficPersonVec_d, agents.data(), size,
                         cudaMemcpyHostToDevice));
  }

  { // edgeData
    size_t sizeD = edgesData.size() * sizeof(LC::EdgeData);
    if (fistInitialization)
      gpuErrchk(
          cudaMalloc((void **)&edgesData_d, sizeD)); // Allocate array on device
    gpuErrchk(cudaMemcpy(edgesData_d, edgesData.data(), sizeD,
                         cudaMemcpyHostToDevice));
  }
  { // laneMap
    size_t sizeL = laneMap.size() * sizeof(uchar);
    if (fistInitialization)
      gpuErrchk(
          cudaMalloc((void **)&laneMap_d, sizeL)); // Allocate array on device
    gpuErrchk(
        cudaMemcpy(laneMap_d, laneMap.data(), sizeL, cudaMemcpyHostToDevice));
    halfLaneMap = laneMap.size() / 2;
  }
  { // intersections
    size_t sizeI = intersections.size() * sizeof(LC::IntersectionData);
    if (fistInitialization)
      gpuErrchk(cudaMalloc((void **)&intersections_d,
                           sizeI)); // Allocate array on device
    gpuErrchk(cudaMemcpy(intersections_d, intersections.data(), sizeI,
                         cudaMemcpyHostToDevice));
  }
  printMemoryUsage();
} //

//! free gpu memories
void finish_cuda(void) {
  //////////////////////////////
  // FINISH
  cudaFree(trafficPersonVec_d);
  cudaFree(indexPathVec_d);
  cudaFree(edgesData_d);
  cudaFree(laneMap_d);
  cudaFree(intersections_d);
} //

void cuda_get_data(std::vector<LC::Agent> &trafficPersonVec,
                   std::vector<LC::EdgeData> &edgesData,
                   std::vector<LC::IntersectionData> &intersections) {
  // copy back people
  size_t size = trafficPersonVec.size() * sizeof(LC::Agent);
  size_t size_edges = edgesData.size() * sizeof(LC::EdgeData);
  size_t size_intersections =
      intersections.size() * sizeof(LC::IntersectionData);

  cudaMemcpy(trafficPersonVec.data(), trafficPersonVec_d, size,
             cudaMemcpyDeviceToHost); // cudaMemcpyHostToDevice
  cudaMemcpy(edgesData.data(), edgesData_d, size_edges,
             cudaMemcpyDeviceToHost); // cudaMemcpyHostToDevice
  cudaMemcpy(intersections.data(), intersections_d, size_intersections,
             cudaMemcpyDeviceToHost); // cudaMemcpyHostToDevice
}

__device__ uint lanemap_pos(const uint currentEdge, const uint edge_length,
                            const uint laneNum, const uint pos_in_lane) {
  uint kMaxMapWidthM = 1024;
  uint num_cell = pos_in_lane / kMaxMapWidthM;
  int tot_num_cell = edge_length / kMaxMapWidthM;
  if (edge_length % kMaxMapWidthM) {
    tot_num_cell += 1;
  }
  return kMaxMapWidthM * currentEdge + kMaxMapWidthM * laneNum * tot_num_cell +
         kMaxMapWidthM * num_cell + pos_in_lane % kMaxMapWidthM;
}

__device__ void calculateGaps(uchar *laneMap, LC::Agent &agent,
                              uint laneToCheck, float &gap_a, float &gap_b,
                              uchar &v_a, uchar &v_b) {

  // CHECK FORWARD
  for (ushort b = agent.posInLaneM - 1; b < agent.edge_length;
       b++) { // NOTE -1 to make sure there is none in at the same level
    auto posToSample =
        lanemap_pos(agent.edge_mid, agent.edge_length, laneToCheck, b);
    if (laneMap[mapToReadShift + posToSample] != 0xFF) {
      gap_a = b - agent.posInLaneM; // m
      v_a = laneMap[mapToReadShift + posToSample] / 3;
      break;
    }
  }
  // CHECK BACKWARD
  for (ushort b = agent.posInLaneM + 1; b > 0;
       b--) { // NOTE -1 to make sure there is none in at the same level
    auto posToSample =
        lanemap_pos(agent.edge_mid, agent.edge_length, laneToCheck, b);
    if (laneMap[mapToReadShift + posToSample] != 0xFF) {
      gap_b = agent.posInLaneM - b; // m
      v_b = laneMap[mapToReadShift + posToSample] / 3;
      break;
    }
  }
}

// TODO : CHECK MULTIPLE LANES
__device__ bool check_space(int space, int eid, int edge_length, uchar *laneMap,
                            uint mapToReadShift) {
  for (auto b = 0; b < space; b++) {
    // just right LANE !!!!!!!
    auto pos = lanemap_pos(eid, edge_length, 0, b);
    auto laneChar =
        laneMap[mapToReadShift + pos]; // get byte of edge (proper line)
    if (laneChar != 0xFF) {
      return false;
    }
  }
  return true;
}

__device__ int deque(int *queue, unsigned &rear) {
  int aid = queue[0];
  for (int i = 0; i < rear - 1; i++) {
    queue[i] = queue[i + 1];
  }
  rear--; // decrement rear
  return aid;
}

__device__ void initialize_agent(int agent_id, LC::Agent &agent,
                                 LC::EdgeData *edgesData, uchar *laneMap,
                                 LC::IntersectionData *intersections) {

  // 1.1  edge case: no available route
  if (agent.route_size == 0) {
    agent.active = 2;
    return;
  }
  // add to corresponding queue
  auto &intersection = intersections[agent.init_intersection];
  intersection.init_queue[intersection.init_queue_rear] = agent_id;
  intersection.init_queue_rear += 1;
  //  atomicAdd(&(intersection.init_queue_rear), 1);

  // initialize agent
  agent.active = 1;
  agent.in_queue = true;
  agent.intersection_id = agent.init_intersection;

  //        bool isSet = false;
  //        do {
  //          if (isSet = atomicCAS(&mutex0, 0, 1) == 0) {
  //            intersection.init_queue[intersection.init_queue_rear] =
  //            agent_id; atomicAdd(&(intersection.init_queue_rear), 1);
  //          }
  //          if (isSet) {
  //            atomicExch(&mutex0, 0);
  //            __syncthreads();
  //          }
  //        } while (!isSet);
}

__device__ bool in_queue(int agent_id, int *queue, unsigned queue_size) {
  for (int i = 0; i < queue_size; ++i) {
    if (queue[i] == agent_id) {
      return true;
    }
  }
  return false;
}

__device__ void check_stagnation(int agent_id, LC::Agent &agent,
                                 LC::IntersectionData *intersections) {
  auto &intersection = intersections[agent.intersection_id];
  if (agent.queue_idx == -1) {
    if (in_queue(agent_id, intersection.init_queue,
                 intersection.init_queue_rear)) {
      return;
    }
    // try to place the agent to the queue again
    intersection.init_queue[intersection.init_queue_rear] = agent_id;
    intersection.init_queue_rear += 1;
  }
  auto &queue = intersection.queue[agent.queue_idx];
  auto &queue_ptr = intersection.pos[agent.queue_idx];
  if (in_queue(agent_id, queue, queue_ptr)) {
    return;
  }
  queue[queue_ptr] = agent_id;
  queue_ptr += 1;
}

// TODO : CHECK NEXT EDGE?
__device__ void check_front_car(LC::Agent &agent, uchar *laneMap,
                                float deltaTime) {

  int numCellsCheck = fmax(15.0f, agent.v * deltaTime); // 15 or speed*time
  ushort byteInLine = (ushort)floor(agent.posInLaneM);

  // a) SAME LINE (BEFORE SIGNALING)
  float s = 20;
  float delta_v = agent.v - agent.max_speed;
  for (ushort b = byteInLine + 1;
       (b < agent.edge_length) && (numCellsCheck > 0); b++, numCellsCheck--) {
    uint posToSample =
        lanemap_pos(agent.edge_mid, agent.edge_length, agent.lane, b);
    auto laneChar = laneMap[mapToReadShift + posToSample];
    if (laneChar != 0xFF) {
      s = ((float)(b - byteInLine)); // m
      delta_v =
          agent.v -
          (laneChar / 3.0f); // laneChar is in 3*ms (to save space in array)
      break;
    }
  }
  agent.s = s;
  agent.delta_v = delta_v;
}

__device__ void update_agent_info(LC::Agent &agent, float deltaTime) {

  // update speed
  float thirdTerm = 0;
  if (agent.delta_v > -0.01) { // car in front and slower than us
    // 2.1.2 calculate dv_dt
    float s_star =
        agent.s_0 +
        fmax(0.0f, (agent.v * agent.T + (agent.v * agent.delta_v) /
                                            (2 * sqrtf(agent.a * agent.b))));

    thirdTerm = powf(((s_star) / (agent.s)), 2);
    agent.slow_down_steps++;
  }
  float dv_dt =
      agent.a * (1.0f - std::pow((agent.v / agent.max_speed), 4) - thirdTerm);
  agent.dv_dt = dv_dt;
  // 2.1.3 update values
  agent.v += dv_dt * deltaTime;
  // if safe enough, speed up instead of creeping
  if ((agent.s > 2 * SOCIAL_DIST) and (agent.v < INIT_SPEED)) {
    agent.v = INIT_SPEED;
  }
  float numMToMove =
      fmax(0.0f, agent.v * deltaTime + 0.5f * (dv_dt)*deltaTime * deltaTime);
  // freeze if below social distance
  if (agent.v < 0 or
      (agent.s - numMToMove < SOCIAL_DIST and agent.v - agent.delta_v < 0.1)) {
    agent.v = 0;
    numMToMove = 0;
  }
  agent.cum_length += numMToMove;
  agent.cum_v += agent.v;
  agent.posInLaneM += numMToMove;
}

__device__ void change_lane(LC::Agent &agent, LC::EdgeData *edgesData,
                            uchar *laneMap) {

  auto &current_edge = edgesData[agent.edge_mid];
  if (agent.posInLaneM > current_edge.length) { // skip if will go to next edge
    return;
  }
  if (current_edge.num_lanes < 2 || agent.v > 0.9 * agent.max_speed) {
    return; // skip if reach the destination/have no lane to change/cruising
            // (avoid periodic lane changing)
  }

  if (
      //          agent.v > 3.0f &&           // at least 10km/h to try to
      //          change lane
      agent.delta_v > -0.01 &&    // decelerating or stuck
      agent.num_steps % 2 == 0) { // check every 2 steps (1 second)

    bool leftLane = agent.lane > 0; // at least one lane on the left
    bool rightLane =
        agent.lane < current_edge.num_lanes - 1; // at least one lane

    if (leftLane && rightLane) {
      if (int(agent.v) % 2 == 0) { // pseudo random for change lane
        rightLane = false;
      }
    }

    ushort laneToCheck = agent.lane - 1;
    if (rightLane) {
      laneToCheck = agent.lane + 1;
    }

    uchar v_a, v_b;
    float gap_a = 1000.0f, gap_b = 1000.0f;
    calculateGaps(laneMap, agent, laneToCheck, gap_a, gap_b, v_a, v_b);

    // Safe distance calculation
    float b1A = 0.05, b2A = 0.15;
    float b1B = 0.15, b2B = 0.40;
    // simParameters.s_0-> critical lead gap
    float g_na_D =
        fmax(agent.s_0, agent.s_0 + b1A * agent.v + b2A * (agent.v - v_a));
    float g_bn_D =
        fmax(agent.s_0, agent.s_0 + b1B * v_b + b2B * (v_b - agent.v));
    if (gap_b < g_bn_D || gap_a < g_na_D) { // gap smaller than critical gap
      return;
    }

    agent.lane = laneToCheck; // CHANGE LINE
    agent.num_lane_change += 1;
  }
}

__device__ uint find_intersetcion_id(LC::Agent &agent,
                                     LC::EdgeData *edgesData) {
  // find the intersection id
  auto &current_edge = edgesData[agent.edge_mid];
  auto &next_edge = edgesData[agent.route[agent.route_ptr + 1]];
  for (unsigned i = 0; i < 2; i++) {
    auto vid = current_edge.vertex[i];
    for (unsigned j = 0; j < 2; j++) {
      if (next_edge.vertex[j] == vid) {
        return vid;
      }
    }
  }
  return 0;
}

__device__ uint find_queue_id(LC::Agent &agent,
                              LC::IntersectionData &intersection) {
  for (unsigned i = 0; i < intersection.num_queue; i++) {
    if (agent.edge_mid == intersection.start_edge[i] and
        agent.route[agent.route_ptr + 1] == intersection.end_edge[i]) {
      return i;
    }
  }
  return 0;
}

__device__ bool update_intersection(int agent_id, LC::Agent &agent,
                                    LC::EdgeData *edgesData,
                                    LC::IntersectionData *intersections) {
  auto &current_edge = edgesData[agent.edge_mid];
  auto extra = agent.posInLaneM - agent.edge_length;
  if (extra < 0) { // does not reach an intersection
    return false;
  }
  agent.cum_length -= extra;                     // remove the extra distance
  if (agent.route_ptr + 1 >= agent.route_size) { // reach destination
    agent.active = 2;
    atomicAdd(&(current_edge.downstream_veh_count), 1);
    int num_steps_in_edge = agent.num_steps - agent.num_steps_entering_edge;
    atomicAdd(&(current_edge.period_cum_travel_steps),
              num_steps_in_edge); // for average travel time calculation
    return false;
  }
  auto intersetcion_id = find_intersetcion_id(agent, edgesData);
  auto &intersection = intersections[intersetcion_id];
  int queue_id = find_queue_id(agent, intersection);
  auto &queue = intersection.queue[queue_id];
  auto &queue_ptr = intersection.pos[queue_id];
  agent.queue_idx = queue_id;
  agent.intersection_id = intersetcion_id;
  agent.in_queue = true;
  agent.v = 0; // in queue vehicle is stopped.
  int num_steps_in_edge = agent.num_steps - agent.num_steps_entering_edge;
  atomicAdd(&(current_edge.period_cum_travel_steps),
            num_steps_in_edge); // for average travel time calculation

  queue[queue_ptr] = agent_id;
  queue_ptr += 1;
  //  atomicAdd(&(queue_ptr), 1);
  atomicAdd(&(current_edge.downstream_veh_count), 1);

  // Synchronization Control
  //  bool isSet = false;
  //  do {
  //    if (isSet = atomicCAS(&mutex, 0, 1) == 0) {
  //      queue[queue_ptr] = agent_id;
  //      atomicAdd(&(queue_ptr), 1);
  //      atomicAdd(&(current_edge.downstream_veh_count), 1);
  //    }
  //    if (isSet) {
  //      atomicExch(&mutex, 0);
  //      __syncthreads();
  //    }
  //  } while (!isSet);
  return true;
}

__device__ void write2lane_map(LC::Agent &agent, LC::EdgeData *edgesData,
                               uchar *laneMap) {
  // write to the lanemap if still on the edge

  auto posToSample = lanemap_pos(agent.edge_mid, agent.edge_length, agent.lane,
                                 agent.posInLaneM);
  uchar vInMpS = (uchar)(agent.v * 3); // speed in m/s to fit in uchar
  laneMap[mapToWriteShift + posToSample] = vInMpS;
}

//! Simulate agents movements on network edges
__global__ void
kernel_trafficSimulation(int numPeople, float currentTime, LC::Agent *agents,
                         LC::EdgeData *edgesData, uchar *laneMap,
                         LC::IntersectionData *intersections, float deltaTime) {

  int p = blockIdx.x * blockDim.x + threadIdx.x;
  if (p >= numPeople) {
    return; // CUDA check (inside margins)
  }
  if (threadIdx.x == 0) {
    mutex = 0;
  }
  //  __syncthreads();

  auto &agent = agents[p];
  // 1. initialization
  if (agent.active == 2) { // agent is already finished
    return;
  }
  // 1.1. check if person should still wait or should start
  if (agent.active == 0) {
    if (agent.time_departure > currentTime) { // wait
      return;
    } else { // its your turn
      initialize_agent(p, agent, edgesData, laneMap, intersections);
      return;
    }
  }

  // 2. Moving
  agent.num_steps++;
  if (agent.in_queue) {
    agent.num_steps_in_queue += 1;
    check_stagnation(p, agent, intersections);
    return;
  }

  // 2.1.1 Find front car
  check_front_car(agent, laneMap, deltaTime);
  // 2.1.2 Update agent information using the front car info
  update_agent_info(agent, deltaTime);
  //  2.1.3 Perform lane changing if necessary
  change_lane(agent, edgesData, laneMap);
  // 2.1.4 check intersection
  bool added2queue = update_intersection(p, agent, edgesData, intersections);
  // 2.1.5 write the updated agent info to lanemap
  if (not added2queue) {
    write2lane_map(agent, edgesData, laneMap);
  }

} //

// TODO : PLACE ON MULTIPLE LANES
__device__ void move2nextEdge(LC::Agent &agent, int numMToMove,
                              LC::EdgeData *edgesData, uchar *laneMap) {

  //  if (not agent.in_queue) {
  //    return;
  //  }
  agent.in_queue = false;
  agent.route_ptr += 1;
  //  atomicAdd(&(agent.route_ptr), 1);
  agent.edge_mid = agent.route[agent.route_ptr];
  agent.posInLaneM = numMToMove;
  agent.lane = 0;
  agent.v = INIT_SPEED; // double initial speed to avoid unnecessary queueing

  auto &current_edge = edgesData[agent.edge_mid];
  agent.edge_id = current_edge.eid;
  agent.max_speed = current_edge.maxSpeedMperSec;
  agent.edge_length = current_edge.length;
  agent.num_steps_entering_edge = agent.num_steps;
  //
  atomicAdd(&(current_edge.upstream_veh_count), 1);

  auto posToSample = lanemap_pos(agent.edge_mid, current_edge.length,
                                 agent.lane, agent.posInLaneM);
  uchar vInMpS = (uchar)(agent.v * 3); // speed in m/s to fit in uchar
  laneMap[mapToWriteShift + posToSample] = vInMpS;
  //
  agent.cum_length += numMToMove;
  agent.num_steps += 1;
}

__device__ bool discharge_queue(LC::IntersectionData &intersection,
                                LC::Agent *trafficPersonVec,
                                LC::EdgeData *edgesData, uchar *laneMap) {
  auto &q1 = intersection.queue[intersection.queue_ptr];
  auto &n1 = intersection.pos[intersection.queue_ptr];

  if (n1 < 1) {
    return false;
  }

  auto aid = q1[0];
  auto &agent = trafficPersonVec[aid];
  if (not agent.in_queue) { // bug walk around: agent has been reassigned to a
                            // queue
    deque(q1, n1);
    return true;
  }

  unsigned eid1 = intersection.end_edge[intersection.queue_ptr];
  int edge_length = edgesData[eid1].length;
  unsigned numMToMove = SOCIAL_DIST;

  bool enough_space =
      check_space(numMToMove + SOCIAL_DIST, eid1, edge_length, laneMap,
                  mapToReadShift); // check social dist ahead

  intersection.max_queue = max(intersection.max_queue, n1);
  bool discharged = false;
  if (enough_space) {
    auto aid = deque(q1, n1);
    move2nextEdge(agent, numMToMove, edgesData,
                  laneMap); // move to the next edge
    discharged = true;
  }
  return discharged;
}

__device__ void place_stop(LC::Agent &agent, LC::EdgeData *edgesData,
                           uchar *laneMap, uint mapToWriteShift) {
  auto &edge = edgesData[agent.edge_mid];
  for (int j = 0; j < SOCIAL_DIST; ++j) {
    auto pos = agent.edge_length - j;
    for (int i = 0; i < edge.num_lanes; ++i) {
      auto posToSample = lanemap_pos(agent.edge_mid, agent.edge_length, i, pos);
      laneMap[mapToWriteShift + posToSample] = 0;
    }
  }
}

__device__ bool discharge_init_agents(unsigned intersection_id,
                                      LC::EdgeData *edgesData,
                                      LC::IntersectionData *intersections,
                                      LC::Agent *trafficPersonVec,
                                      uchar *laneMap) {
  auto &intersection = intersections[intersection_id];
  auto &init_queue = intersection.init_queue;
  auto &rear_ptr = intersection.init_queue_rear;
  if (rear_ptr < 1) {
    return false;
  }
  bool discharged = false;
  auto aid = init_queue[0];
  auto &agent = trafficPersonVec[aid];
  if (not agent.in_queue) { // bug walk around: agent has been reassigned to a
                            // queue
    deque(init_queue, rear_ptr);
    return true;
  }

  auto &first_edge = edgesData[agent.route[0]];
  unsigned numMToMove = SOCIAL_DIST;
  bool enough_space = check_space(numMToMove + SOCIAL_DIST, agent.route[0],
                                  first_edge.length, laneMap,
                                  mapToReadShift); // check social dist ahead
  if (enough_space) {
    aid = deque(init_queue, rear_ptr);
    move2nextEdge(agent, numMToMove, edgesData, laneMap);
    discharged = true;
  }
  // update waiting steps for all other agents
  for (int i = 0; i < rear_ptr; ++i) {
    auto aid = init_queue[i];
    auto &agent = trafficPersonVec[aid];
    agent.initial_waited_steps += 1;
  }
  return discharged;
}

__device__ void check_queues(unsigned intersection_id, LC::EdgeData *edgesData,
                             LC::IntersectionData *intersections,
                             LC::Agent *trafficPersonVec, uchar *laneMap) {
  auto &intersection = intersections[intersection_id];
  bool discharged = false;
  for (int i = 0; i < intersection.num_queue + 1; ++i) {
    if (intersection.queue_ptr > intersection.num_queue - 1) {
      intersection.queue_ptr = 0; // reset
      discharged = discharge_init_agents(
          intersection_id, edgesData, intersections, trafficPersonVec, laneMap);
    }
    if (not discharged) {
      discharged =
          discharge_queue(intersection, trafficPersonVec, edgesData, laneMap);
    }
    intersection.queue_ptr += 1;
    if (discharged) {
      break;
    }
  }
}

//! Simulate agents movements on intersections
__global__ void
kernel_intersectionOneSimulation(uint numIntersections, LC::EdgeData *edgesData,
                                 LC::IntersectionData *intersections,
                                 LC::Agent *agents, uchar *laneMap) {

  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i >= numIntersections) {
    return; // CUDA check (inside margins)
  }
  check_queues(i, edgesData, intersections, agents, laneMap);

  // add a stop sign for full queues
  auto &intersection = intersections[i];
  for (unsigned j = 0; j < intersection.num_queue; j++) {
    auto num_cars = intersection.pos[j];
    if (num_cars > 0) {
      auto &q1 = intersection.queue[j];
      auto &agent = agents[q1[0]];
      place_stop(agent, edgesData, laneMap, mapToWriteShift);
    }
  }
}

void cuda_simulate(float currentTime, uint numPeople, uint numIntersections,
                   float deltaTime, int numBlocks, int threadsPerBlock) {

  ////////////////////////////////////////////////////////////
  // 1. CHANGE MAP: set map to use and clean the other
  if (readFirstMapC) {
    mapToReadShift = 0;
    mapToWriteShift = halfLaneMap;
    gpuErrchk(
        cudaMemset(&laneMap_d[halfLaneMap], -1,
                   halfLaneMap * sizeof(unsigned char))); // clean second half
  } else {
    mapToReadShift = halfLaneMap;
    mapToWriteShift = 0;
    gpuErrchk(
        cudaMemset(&laneMap_d[0], -1,
                   halfLaneMap * sizeof(unsigned char))); // clean first half
  }
  readFirstMapC = !readFirstMapC; // next iteration invert use

  std::random_device
      rd; // Will be used to obtain a seed for the random number engine
  std::mt19937 gen(rd()); // Standard mersenne_twister_engine seeded with rd()
  std::uniform_int_distribution<> random_bool(0, 1);
  // random assign which to go
  intersectionBench.startMeasuring();
  kernel_intersectionOneSimulation<<<numBlocks, threadsPerBlock>>>(
      numIntersections, edgesData_d, intersections_d, trafficPersonVec_d,
      laneMap_d);
  gpuErrchk(cudaPeekAtLastError());
  intersectionBench.stopMeasuring();

  peopleBench.startMeasuring();
  // Simulate people.
  kernel_trafficSimulation<<<numBlocks, threadsPerBlock>>>(
      numPeople, currentTime, trafficPersonVec_d, edgesData_d, laneMap_d,
      intersections_d, deltaTime);
  gpuErrchk(cudaPeekAtLastError());
  peopleBench.stopMeasuring();
  //    if (random_bool(gen)){
  //        peopleBench.startMeasuring();
  //        // Simulate people.
  //        kernel_trafficSimulation<<<numBlocks, threadsPerBlock>>>(
  //                numPeople, currentTime, trafficPersonVec_d, edgesData_d,
  //                laneMap_d,
  //                        intersections_d, deltaTime);
  //        gpuErrchk(cudaPeekAtLastError());
  //        peopleBench.stopMeasuring();
  //
  //        // Simulate intersections.
  //        intersectionBench.startMeasuring();
  //        kernel_intersectionOneSimulation<<<numBlocks, threadsPerBlock>>>(
  //                numIntersections, edgesData_d, intersections_d,
  //                trafficPersonVec_d,
  //                        laneMap_d);
  //        gpuErrchk(cudaPeekAtLastError());
  //        intersectionBench.stopMeasuring();
  //    }
  //    else{
  //        // Simulate intersections.
  //        intersectionBench.startMeasuring();
  //        kernel_intersectionOneSimulation<<<numBlocks, threadsPerBlock>>>(
  //                numIntersections, edgesData_d, intersections_d,
  //                trafficPersonVec_d,
  //                        laneMap_d);
  //        gpuErrchk(cudaPeekAtLastError());
  //        intersectionBench.stopMeasuring();
  //
  //        peopleBench.startMeasuring();
  //        // Simulate people.
  //        kernel_trafficSimulation<<<numBlocks, threadsPerBlock>>>(
  //                numPeople, currentTime, trafficPersonVec_d, edgesData_d,
  //                laneMap_d,
  //                        intersections_d, deltaTime);
  //        gpuErrchk(cudaPeekAtLastError());
  //        peopleBench.stopMeasuring();
  //
  //    }

} //
