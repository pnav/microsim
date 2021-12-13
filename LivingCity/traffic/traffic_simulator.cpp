#include "traffic_simulator.h"

void printPercentageMemoryUsed() {
  // TODO
}

namespace LC {

const float intersectionClearance = 7.8f;
const bool calculatePollution = true;

extern TrafficSimulator::TrafficSimulator(
    RoadGraph *originalRoadGraph, const IDMParameters &inputSimParameters,
    std::shared_ptr<Network> network, const std::string &save_path)
    : simParameters_(inputSimParameters) {
  simRoadGraph_ = new RoadGraph(*originalRoadGraph);
  network_ = network;
  lanemap_ = Lanemap(network->street_graph());
  save_path_ = save_path;
  boost::filesystem::path dir(save_path_);
  if (boost::filesystem::create_directory(dir)) {
    std::cout << "Save Dict Directory Created: " << save_path_ << std::endl;
  }
}

TrafficSimulator::~TrafficSimulator() { delete simRoadGraph_; }

void TrafficSimulator::reset_agent() {
  for (int p = 0; p < agents_.size(); p++) {
    agents_[p].active = 0;
  }
}

void TrafficSimulator::load_agents() {
  auto totalNumPeople = network_->totalNumPeople();
  if (network_->totalNumPeople() == 0) {
    printf("ERROR: No agent to simulate\n");
    return;
  }
  agents_.clear();

  auto dep_times = network_->dep_time();
  auto ods = network_->od_pairs();
  auto graph = network_->street_graph();

  for (int i = 0; i < totalNumPeople; i++) {
    auto src_vertex = ods[i][0];
    auto tgt_vertex = ods[i][1];
    float dep_time = dep_times[i];

    Agent agent(src_vertex, tgt_vertex, dep_time, simParameters_);
    agent.indexPathInit = graph->person_to_init_edge_[i];
    agent.indexPathCurr = graph->person_to_init_edge_[i];
    agents_.emplace_back(agent);
  }

  printf("Load agents: People %d\n", totalNumPeople);
}

// void TrafficSimulator::createLaneMap() {
//  b18TrafficLaneMap.createLaneMap(*simRoadGraph, laneMap, edgesData,
//                                  intersections, trafficLights,
//                                  laneMapNumToEdgeDesc, edgeDescToLaneMapNum);
//} //
//
// void TrafficSimulator::createLaneMapSP(
//    const std::shared_ptr<abm::Graph> &graph_) { //
//  b18TrafficLaneMap.createLaneMapSP(graph_, laneMap, edgesData, intersections,
//                                    trafficLights, laneMapNumToEdgeDescSP,
//                                    edgeDescToLaneMapNumSP,
//                                    edgeIdToLaneMapNum);
//}
//
// void TrafficSimulator::generateCarPaths(bool useJohnsonRouting) { //
//  if (useJohnsonRouting) {
//    printf("***generateCarPaths Start generateRoute Johnson\n");
//    B18TrafficJohnson::generateRoutes(simRoadGraph->myRoadGraph_BI,
//                                      agents_, indexPathVec,
//                                      edgeDescToLaneMapNum, 0);
//  } else {
//    printf("***generateCarPaths Start generateRoutesMulti Disktra\n");
//    B18TrafficDijstra::generateRoutesMulti(simRoadGraph->myRoadGraph_BI,
//                                           agents_, indexPathVec,
//                                           edgeDescToLaneMapNum, 0);
//  }
//
//} //
//
//////////////////////////////////////////////////////
////// GPU
//////////////////////////////////////////////////////
void TrafficSimulator::simulateInGPU(
    const std::vector<abm::graph::edge_id_t> &paths_SP, float startTime,
    float endTime) {

  Benchmarker passesBench("Simulation passes");
  Benchmarker finishCudaBench("Cuda finish");

  Benchmarker microsimulationInGPU("Microsimulation_in_GPU", true);
  microsimulationInGPU.startMeasuring();

  Benchmarker roadGenerationBench("Road generation");
  Benchmarker initCudaBench("Init Cuda step");
  Benchmarker simulateBench("Simulation step");
  Benchmarker getDataBench("Data retrieve step");
  Benchmarker shortestPathBench("Shortest path step");
  Benchmarker fileOutput("File_output", true);

  roadGenerationBench.startMeasuring();
  lanemap_.read_path(paths_SP);

  roadGenerationBench.stopAndEndBenchmark();

  //  Benchmarker edgeOutputting("Edge outputting");
  //  edgeOutputting.startMeasuring();
  //
  //  int index = 0;
  //  std::vector<uint> u(graph_->edges_.size());
  //  std::vector<uint> v(graph_->edges_.size());
  //  for (auto const &x : graph_->edges_) {
  //    abm::graph::vertex_t vertex_u = std::get<0>(std::get<0>(x));
  //    abm::graph::vertex_t vertex_v = std::get<1>(std::get<0>(x));
  //    u[index] = vertex_u;
  //    v[index] = vertex_v;
  //    index++;
  //  }
  //
  //  // save avg_edge_vel vector to file
  //  std::string name_u = "./edges_u.txt";
  //  std::ofstream output_file_u(name_u);
  //  std::ostream_iterator<uint> output_iterator_u(output_file_u, "\n");
  //  std::copy(u.begin(), u.end(), output_iterator_u);
  //
  //  // save avg_edge_vel vector to file
  //  std::string name_v = "./edges_v.txt";
  //  std::ofstream output_file_v(name_v);
  //  std::ostream_iterator<uint> output_iterator_v(output_file_v, "\n");
  //  std::copy(v.begin(), v.end(), output_iterator_v);
  //  std::cout << "Wrote edge vertices files!" << std::endl;
  //  edgeOutputting.stopAndEndBenchmark();

  /////////////////////////////////////
  // 1. Init Cuda
  initCudaBench.startMeasuring();
  bool fistInitialization = true;
  uint count = 0;

  std::vector<uint> indexPathVec = lanemap_.indexPathVec();
  std::vector<B18EdgeData> edgesData = lanemap_.edgesData();
  std::vector<uchar> laneMap = lanemap_.lanemap_array();
  std::cout << "edgesData size" << edgesData.size() << std::endl;
  std::vector<B18IntersectionData> intersections = lanemap_.intersections();
  std::vector<uchar> trafficLights = lanemap_.traffic_lights();
  auto graph_ = network_->street_graph();

  std::cout << "Traffic person vec size = " << agents_.size() << std::endl;
  std::cout << "Index path vec size = " << indexPathVec.size() << std::endl;
  std::cout << "EdgesData size = " << edgesData.size() << std::endl;
  std::cout << "LaneMap size = " << laneMap.size() << std::endl;
  std::cout << "Intersections size = " << intersections.size() << std::endl;

  b18InitCUDA(fistInitialization, agents_, indexPathVec, edgesData, laneMap,
              trafficLights, intersections, 0, 12,
              accSpeedPerLinePerTimeInterval, numVehPerLinePerTimeInterval,
              deltaTime);

  initCudaBench.stopAndEndBenchmark();

  simulateBench.startMeasuring();

  float currentTime = 23.99f * 3600.0f;
  for (int p = 0; p < agents_.size(); p++) {
    if (currentTime > agents_[p].time_departure) {
      currentTime = agents_[p].time_departure;
    }
  }

  /*
  for (int i = 0; i < edgesData.size(); i++) {
      std::cout << "i = " << i << "speed = " << edgesData[i].maxSpeedMperSec
  << "\n";
  }
  */
  int numInt = currentTime / deltaTime; // floor
  currentTime = numInt * deltaTime;
  uint steps = (currentTime - startTime) / deltaTime;

  // start as early as starting time
  if (currentTime < startTime) { // as early as the starting time
    currentTime = startTime;
  }

  QTime timer;
  timer.start();
  // Reset people to inactive.
  b18ResetPeopleLanesCUDA(agents_.size());
  // 2. Execute
  printf("First time_departure %f\n", currentTime);
  QTime timerLoop;

  int numBlocks = ceil(agents_.size() / 384.0f);
  int threadsPerBlock = 384;
  std::cout << "Running trafficSimulation with the following configuration:"
            << std::endl
            << ">  Number of people: " << agents_.size() << std::endl
            << ">  Number of blocks: " << numBlocks << std::endl
            << ">  Number of threads per block: " << threadsPerBlock
            << std::endl;

  std::cerr << "Running main loop from " << (startTime / 3600.0f) << " to "
            << (endTime / 3600.0f) << " with " << agents_.size() << " person..."
            << std::endl;

  std::vector<std::vector<unsigned>> edge_upstream_count;
  std::vector<std::vector<unsigned>> edge_downstream_count;
  std::vector<std::vector<unsigned>> intersection_count;
  while (currentTime < endTime) {
    // std::cout << "Current Time " << currentTime << "\n";
    // std::cout << "count " << count << "\n";
    count++;
    //    if (count % 1800 == 0) {
    //      std::cerr << std::fixed << std::setprecision(2)
    //                << "Current time: " << (currentTime / 3600.0f) << " ("
    //                << (100.0f -
    //                    (100.0f * (endTime - currentTime) / (endTime -
    //                    startTime)))
    //                << "%)"
    //                << " with " << (timerLoop.elapsed() / 1800.0f)
    //                << " ms per simulation step (average over 1800)"
    //                << "\r";
    //      timerLoop.restart();
    //    }
    b18SimulateTrafficCUDA(currentTime, agents_.size(), intersections.size(),
                           deltaTime, simParameters_, numBlocks,
                           threadsPerBlock);
    //    std::cout<<currentTime<<std::endl;

    if (count % 10 == 0) {
      Benchmarker getDataCudatrafficPersonAndEdgesData(
          "Get data agents_ and edgesData (first time)");
      getDataCudatrafficPersonAndEdgesData.startMeasuring();
      b18GetDataCUDA(agents_, edgesData, intersections);
      getDataCudatrafficPersonAndEdgesData.stopAndEndBenchmark();

      std::vector<unsigned> upstream_counts(graph_->edges_.size());
      std::vector<unsigned> downstream_counts(graph_->edges_.size());

      auto edgeDescToLaneMapNumSP = lanemap_.edgeDescToLaneMapNum();
      for (auto const &x : graph_->edges_) {
        auto ind = edgeDescToLaneMapNumSP[x.second];
        auto edge_vertices = std::get<1>(x)->first;
        auto edge_id =
            graph_->edge_ids_[get<0>(edge_vertices)][get<1>(edge_vertices)];
        upstream_counts[edge_id] = edgesData[ind].upstream_veh_count;
        downstream_counts[edge_id] = edgesData[ind].downstream_veh_count;
      }
      edge_upstream_count.emplace_back(upstream_counts);
      edge_downstream_count.emplace_back(downstream_counts);

      // intersection monitoring
      auto &intersection2 = intersections[2];
      std::cout << count << "; "  << intersection2.max_queue << "; " << intersection2.pos[2]
                << std::endl;
      for (int i = 0; i < 20; ++i) {
        std::cout << intersection2.pos[i] << "; ";
      }
      auto edgeidmap = lanemap_.edgeIdToLaneMapNum();
      std::cout << edgeidmap[0] << ";" << edgeidmap[1] << ";" << edgeidmap[4]
                << "count" << std::endl;
      for (int i = 0; i < 20; ++i) {
        auto lanemap_number = intersection2.start_edge[i];
        std::cout << lanemap_number << "; ";
      }
      std::cout << "start" << std::endl;

      for (int i = 0; i < 20; ++i) {
        std::cout << intersection2.end_edge[i] << "; ";
      }
      std::cout << "end" << std::endl;

      std::vector<unsigned> intersection_counts(intersection2.num_queue);
      for (unsigned i = 0; i < intersection2.num_queue; i++) {
        intersection_counts[i] = intersection2.pos[i];
      }
      intersection_count.emplace_back(intersection_counts);
      //      timerLoop.restart();
    }

    currentTime += deltaTime;
  }
  std::cout << "Total # iterations = " << count << "\n";
  // std::cerr << std::setw(90) << " " << "\rDone" << std::endl;
  simulateBench.stopAndEndBenchmark();
  getDataBench.startMeasuring();

  // 3. Finish

  Benchmarker getDataCudatrafficPersonAndEdgesData(
      "Get data agents_ and edgesData (second time)");
  b18GetDataCUDA(agents_, edgesData, intersections);
  getDataCudatrafficPersonAndEdgesData.startMeasuring();
  getDataCudatrafficPersonAndEdgesData.stopAndEndBenchmark();
  b18GetSampleTrafficCUDA(accSpeedPerLinePerTimeInterval,
                          numVehPerLinePerTimeInterval);
  //  {
  //    // debug
  //    float totalNumSteps = 0;
  //    float totalCO = 0;
  //
  //    for (int p = 0; p < agents_.size(); p++) {
  //      // std::cout << "num_steps " << agents_[p].num_steps << " for
  //      // person " << p << "\n";
  //      totalNumSteps += agents_[p].num_steps;
  //      totalCO += agents_[p].co;
  //    }
  //
  //    auto avgTravelTime = (totalNumSteps * deltaTime) /
  //                    (agents_.size() * 60.0f); // in min
  //    printf("Total num steps %.1f Avg %.2f min Avg CO %.2f\nSimulation time
  //    =
  //    "
  //           "%d ms\n",
  //           totalNumSteps, avgTravelTime, totalCO / agents_.size(),
  //           timer.elapsed());
  //
  //    // write paths to file so that we can just load them instead
  //    // std::ofstream output_file("./num_steps.txt");
  //    // output_file << totalNumSteps;
  //  }
  //
  // calculateAndDisplayTrafficDensity(nP);
  // savePeopleAndRoutes(nP);
  // G::global()["cuda_render_displaylist_staticRoadsBuildings"] = 1;//display
  // list
  fileOutput.startMeasuring();
  save_edges(edge_upstream_count, edge_downstream_count);
  save_intersection(intersection_count);
  savePeopleAndRoutesSP(0, graph_, paths_SP, (int)0, (int)1);
  fileOutput.stopAndEndBenchmark();
  getDataBench.stopAndEndBenchmark();

  passesBench.stopAndEndBenchmark();
  finishCudaBench.startMeasuring();
  b18FinishCUDA();
  G::global()["cuda_render_displaylist_staticRoadsBuildings"] =
      3; // kill display list

  microsimulationInGPU.stopAndEndBenchmark();
  finishCudaBench.stopAndEndBenchmark();
} //

void TrafficSimulator::writePeopleFile(
    int numOfPass, const std::shared_ptr<abm::Graph> &graph_, int start_time,
    int end_time, const std::vector<Agent> &agents_, float deltaTime) {
  QFile peopleFile(QString::fromStdString(save_path_) + "people" +
                   QString::number(start_time) + "to" +
                   QString::number(end_time) + ".csv");
  if (peopleFile.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
    std::cout << "> Saving People file... (size " << agents_.size() << ")"
              << std::endl;
    QTextStream streamP(&peopleFile);
    streamP << "p,init_intersection,end_intersection,time_departure,traveled_"
               "time(s),inqueue_time,slowdown_steps,"
               "index,v,dv_dt,front_v,m2move,space,third_term,max_speed, "
               "cum_distance,avg_v(m/s),status,lane_number,change_lane, eid, "
               "located_eid\n";

    for (int p = 0; p < agents_.size(); p++) {
      streamP << p;
      streamP << "," << agents_[p].init_intersection;
      streamP << "," << agents_[p].end_intersection;
      streamP << "," << agents_[p].time_departure;
      streamP << "," << agents_[p].num_steps;
      streamP << "," << agents_[p].num_steps_in_queue;
      streamP << "," << agents_[p].slow_down_steps;
      streamP << "," << agents_[p].indexPathCurr;
      streamP << "," << agents_[p].v;
      streamP << "," << agents_[p].dv_dt;
      streamP << "," << agents_[p].front_speed;
      streamP << "," << agents_[p].m2move;
      streamP << "," << agents_[p].s;
      streamP << "," << agents_[p].thirdTerm;
      streamP << "," << agents_[p].maxSpeedMperSec;
      streamP << "," << agents_[p].cum_length;
      streamP << "," << (agents_[p].cum_v / agents_[p].num_steps);
      streamP << "," << agents_[p].active;
      streamP << "," << agents_[p].numOfLaneInEdge;
      streamP << "," << agents_[p].num_lane_change;
      streamP << "," << agents_[p].currentEdge;
      streamP << "," << agents_[p].located_eid;

      streamP << "\n";
    }

    peopleFile.close();
    std::cout << "> Finished saving People file." << std::endl;
  }
}

bool isLastEdgeOfPath(abm::graph::edge_id_t edgeInPath) {
  return edgeInPath == -1;
}

void TrafficSimulator::writeRouteFile(
    int numOfPass, const std::vector<abm::graph::edge_id_t> &paths_SP,
    int start_time, int end_time) {
  QFile routeFile(QString::fromStdString(save_path_) + "route" +
                  QString::number(start_time) + "to" +
                  QString::number(end_time) + ".csv");
  if (routeFile.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
    std::cout << "> Saving Route file..." << std::endl;
    QHash<uint, uint> laneMapNumCount;
    QTextStream streamR(&routeFile);
    streamR << "p:route\n";
    int lineIndex = 0;
    int peopleIndex = 0;
    streamR << lineIndex << ":[";
    for (const abm::graph::edge_id_t &edgeInPath : paths_SP) {
      if (isLastEdgeOfPath(edgeInPath)) {
        streamR << "]\n";
        lineIndex++;
        if (peopleIndex != paths_SP.size() - 1) {
          streamR << lineIndex << ":[";
        }
      } else {
        streamR << edgeInPath << ",";
      }
      peopleIndex++;
    }
    routeFile.close();
  }
  std::cout << "> Finished saving Route file." << std::endl;
}

void TrafficSimulator::writeIndexPathVecFile(
    int numOfPass, int start_time, int end_time,
    const std::vector<uint> &indexPathVec) {
  QFile indexPathVecFile(QString::fromStdString(save_path_) + "indexPathVec" +
                         QString::number(start_time) + "to" +
                         QString::number(end_time) + ".csv");
  if (indexPathVecFile.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
    std::cout << "> Saving indexPathVec (size " << indexPathVec.size() << ")..."
              << std::endl;
    QTextStream indexPathVecStream(&indexPathVecFile);
    indexPathVecStream << "indexPathVec\n";

    for (auto const &elemIndexPathVec : indexPathVec) {
      indexPathVecStream << elemIndexPathVec << "\n";
    }

    indexPathVecFile.close();
  }
  std::cout << "> Finished saving indexPathVec..." << std::endl;
}

void TrafficSimulator::savePeopleAndRoutesSP(
    int numOfPass, const std::shared_ptr<abm::Graph> &graph_,
    const std::vector<abm::graph::edge_id_t> &paths_SP, int start_time,
    int end_time) {
  bool enableMultiThreading = true;
  const bool saveToFile = true;

  if (!saveToFile) {
    return;
  }

  writePeopleFile(numOfPass, graph_, start_time, end_time, agents_, deltaTime);
  writeRouteFile(numOfPass, paths_SP, start_time, end_time);
  writeIndexPathVecFile(numOfPass, start_time, end_time,
                        lanemap_.indexPathVec());

  //  if (enableMultiThreading) {
  //    //    std::cout << "Saving People, Route and IndexPathVec files..." <<
  //    //    std::endl; std::thread
  //    //    threadWritePeopleFile(&TrafficSimulator::writePeopleFile,
  //    numOfPass,
  //    //    graph_,
  //    //                                      start_time, end_time, agents_,
  //    //                                      deltaTime);
  //    ////    std::thread
  //    threadWriteRouteFile(&TrafficSimulator::writeRouteFile,
  //    ///numOfPass, paths_SP, / start_time,
  //    ///end_time); /    auto indexPathVec = lanemap_.indexPathVec(); /
  //    ///std::thread
  //    ///threadWriteIndexPathVecFile(&TrafficSimulator::writeIndexPathVecFile,
  //    ///numOfPass, /                                            start_time,
  //    ///end_time, indexPathVec);
  //    //    threadWritePeopleFile.join();
  //    ////    threadWriteRouteFile.join();
  //    ////    threadWriteIndexPathVecFile.join();
  //    //    std::cout << "Finished saving People, Route and IndexPathVec
  //    files."
  //    //              << std::endl;
  //  } else {
  //    writePeopleFile(numOfPass, graph_, start_time, end_time, agents_,
  //                    deltaTime);
  //    writeRouteFile(numOfPass, paths_SP, start_time, end_time);
  //        writeIndexPathVecFile(numOfPass,
  //        ////                                            start_time,
  //        end_time,
  //        ///indexPathVec);
  //  }
}

void TrafficSimulator::save_edges(
    const std::vector<std::vector<unsigned>> &edge_upstream_count,
    const std::vector<std::vector<unsigned>> &edge_downstream_count) {

  std::ofstream upFile(save_path_ + "upstream_count.csv");
  std::ofstream downFile(save_path_ + "downstream_count.csv");

  for (const auto &et : edge_upstream_count) {
    for (const auto &e : et) {
      upFile << e << ",";
    }
    upFile << "\n";
  }

  for (const auto &et : edge_downstream_count) {
    for (const auto &e : et) {
      downFile << e << ",";
    }
    downFile << "\n";
  }
}

void TrafficSimulator::save_intersection(
    const std::vector<std::vector<unsigned>> &intersection_count) {

  std::ofstream file(save_path_ + "intersection_count.csv");

  for (const auto &et : intersection_count) {
    for (const auto &e : et) {
      file << e << ",";
    }
    file << "\n";
  }
}

} // namespace LC
