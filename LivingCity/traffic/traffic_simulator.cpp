#include "traffic_simulator.h"
#include "pandana_ch/accessibility.h"

namespace LC {

TrafficSimulator::TrafficSimulator(std::shared_ptr<Network> network,
                                   std::shared_ptr<OD> od,
                                   std::shared_ptr<Lanemap> lanemap,
                                   const std::string &save_path) {
  network_ = network;
  od_ = od;
  lanemap_ = lanemap;
  save_path_ = save_path;
  boost::filesystem::path dir(save_path_);
  if (boost::filesystem::create_directory(dir)) {
    std::cout << "Save Dict Directory Created: " << save_path_ << std::endl;
  }
  route_finding_();
}

void TrafficSimulator::route_finding_() {
  // compute routes use contraction hierarchy
  auto graph_ch = std::make_shared<MTC::accessibility::Accessibility>(
      network_->num_vertices(), network_->edge_vertices(),
      network_->edge_weights(), false);

  auto &agents = od_->agents();
  std::vector<long> sources, targets;
  for (const auto &agent : agents) {
    sources.emplace_back(agent.init_intersection);
    targets.emplace_back(agent.end_intersection);
  }

  auto node_sequence = graph_ch->Routes(sources, targets, 0);
  auto &eid2mid = lanemap_->eid2mid();
  //  std::cout << "# of paths = " << all_paths_ch.size() << " \n";

  // add routes to each agent
  for (int i = 0; i < node_sequence.size(); i++) {
    auto &agent = agents[i];
    if (node_sequence[i].size() > 100) {
      std::cerr << "Warning: Agent " << i << "need to go through "
                << node_sequence[i].size() << " edges!" << std::endl;
    }
    if (node_sequence[i].size() == 0) {
      std::cerr << "Warning: Agent " << i << "has no route! " << std::endl;
    }

    for (int j = 0; j < node_sequence[i].size() - 1; j++) {
      auto vertex_from = node_sequence[i][j];
      auto vertex_to = node_sequence[i][j + 1];
      auto eid = network_->edge_id(vertex_from, vertex_to);
      auto mid = eid2mid.at(eid);
      agent.route[agent.route_size] = mid;
      agent.route_size++;
    }
  }
}

//
////////////////////////////////////////////////////////
//////// GPU
////////////////////////////////////////////////////////
void TrafficSimulator::simulateInGPU(float startTime, float endTime) {

  Benchmarker passesBench("Simulation passes");
  Benchmarker finishCudaBench("Cuda finish");

  Benchmarker microsimulationInGPU("Microsimulation_in_GPU", true);
  microsimulationInGPU.startMeasuring();

  Benchmarker initCudaBench("Init Cuda step");
  Benchmarker simulateBench("Simulation step");
  Benchmarker getDataBench("Data retrieve step");
  Benchmarker shortestPathBench("Shortest path step");
  Benchmarker fileOutput("File_output", true);


  /////////////////////////////////////
  // 1. Init Cuda
  initCudaBench.startMeasuring();
  auto & agents = od_->agents();
  auto & edgesData = lanemap_->edgesData();
  auto & lanemap_data = lanemap_->lanemap_array();
  auto & intersections = lanemap_->intersections();

  std::cout << "Traffic person vec size = " << agents.size()
            << std::endl;
  std::cout << "EdgesData size = " << edgesData.size() << std::endl;
  std::cout << "LaneMap size = " << lanemap_data.size()
            << std::endl;
  std::cout << "Intersections size = " << intersections.size()
            << std::endl;

  init_cuda(true, agents, edgesData,
            lanemap_data, intersections, deltaTime_);

  initCudaBench.stopAndEndBenchmark();


  simulateBench.startMeasuring();
  int numBlocks = ceil(agents.size() / 384.0f);

  std::cout << "Running trafficSimulation with the following configuration:"
            << std::endl
            << ">  Number of people: " << agents.size() << std::endl
            << ">  Number of blocks: " << numBlocks << std::endl
            << ">  Number of threads per block: " << CUDAThreadsPerBlock
            << std::endl;

  std::cerr << "Running main loop from " << (startTime / 3600.0f) << " to "
            << (endTime / 3600.0f) << " with " << agents.size()
            << "person... " << std::endl;

  // 2. Run GPU Simulation
  while (startTime < endTime) {
      cuda_simulate (startTime, agents.size (),
                     intersections.size (), deltaTime_, numBlocks,
                     CUDAThreadsPerBlock);

      startTime += deltaTime;
  }

  // 3. Get data from cuda
  cuda_get_data(agents, edgesData, intersections);

  // 4. Store data to local disk


    //    std::cout<<currentTime<<std::endl;

//    if (count % 10 == 0) {
//      Benchmarker getDataCudatrafficPersonAndEdgesData(
//          "Get data agents_ and edgesData (first time)");
//      getDataCudatrafficPersonAndEdgesData.startMeasuring();
//      cuda_get_data(agents_, edgesData, intersections);
//      getDataCudatrafficPersonAndEdgesData.stopAndEndBenchmark();
//
//      std::vector<unsigned> upstream_counts(graph_->edges_.size());
//      std::vector<unsigned> downstream_counts(graph_->edges_.size());
//
//      auto edgeDescToLaneMapNumSP = lanemap_.edgeDescToLaneMapNum();
//      for (auto const &x : graph_->edges_) {
//        auto ind = edgeDescToLaneMapNumSP[x.second];
//        auto edge_vertices = std::get<1>(x)->first;
//        auto edge_id =
//            graph_->edge_ids_[get<0>(edge_vertices)][get<1>(edge_vertices)];
//        upstream_counts[edge_id] = edgesData[ind].upstream_veh_count;
//        downstream_counts[edge_id] = edgesData[ind].downstream_veh_count;
//      }
//      edge_upstream_count.emplace_back(upstream_counts);
//      edge_downstream_count.emplace_back(downstream_counts);
//
//      // intersection monitoring
//      auto &intersection2 = intersections[2];
//      std::cout << count << "; " << intersection2.max_queue << "; "
//                << intersection2.pos[2] << std::endl;
//      for (int i = 0; i < 20; ++i) {
//        std::cout << intersection2.pos[i] << "; ";
//      }
//      auto edgeidmap = lanemap_.edgeIdToLaneMapNum();
//      std::cout << edgeidmap[0] << ";" << edgeidmap[1] << ";" << edgeidmap[4]
//                << "count" << std::endl;
//      for (int i = 0; i < 20; ++i) {
//        auto lanemap_number = intersection2.start_edge[i];
//        std::cout << lanemap_number << "; ";
//      }
//      std::cout << "start" << std::endl;
//
//      for (int i = 0; i < 20; ++i) {
//        std::cout << intersection2.end_edge[i] << "; ";
//      }
//      std::cout << "end" << std::endl;
//
//      std::vector<unsigned> intersection_counts(intersection2.num_queue);
//      for (unsigned i = 0; i < intersection2.num_queue; i++) {
//        intersection_counts[i] = intersection2.pos[i];
//      }
//      intersection_count.emplace_back(intersection_counts);
//      //      timerLoop.restart();
//    }


  }
//  std::cout << "Total # iterations = " << count << "\n";
//  // std::cerr << std::setw(90) << " " << "\rDone" << std::endl;
//  simulateBench.stopAndEndBenchmark();
//  getDataBench.startMeasuring();
//
//  // 3. Finish
//
//  Benchmarker getDataCudatrafficPersonAndEdgesData(
//      "Get data agents_ and edgesData (second time)");
//  cuda_get_data(agents_, edgesData, intersections);
//  getDataCudatrafficPersonAndEdgesData.startMeasuring();
//  getDataCudatrafficPersonAndEdgesData.stopAndEndBenchmark();
//  b18GetSampleTrafficCUDA(accSpeedPerLinePerTimeInterval,
//                          numVehPerLinePerTimeInterval);
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
  //    auto avgTravelTime = (totalNumSteps * deltaTime_) /
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
//  fileOutput.startMeasuring();
//  save_edges(edge_upstream_count, edge_downstream_count);
//  save_intersection(intersection_count);
//  savePeopleAndRoutesSP(0, graph_, paths_SP, (int)0, (int)1);
//  fileOutput.stopAndEndBenchmark();
//  getDataBench.stopAndEndBenchmark();
//
//  passesBench.stopAndEndBenchmark();
//  finishCudaBench.startMeasuring();
//  finish_cuda();
//  G::global()["cuda_render_displaylist_staticRoadsBuildings"] =
//      3; // kill display list
//
//  microsimulationInGPU.stopAndEndBenchmark();
//  finishCudaBench.stopAndEndBenchmark();
} //
//
// void TrafficSimulator::writePeopleFile(
//    int numOfPass, const std::shared_ptr<abm::Graph> &graph_, int start_time,
//    int end_time, const std::vector<Agent> &agents_, float deltaTime_) {
//  QFile peopleFile(QString::fromStdString(save_path_) + "people" +
//                   QString::number(start_time) + "to" +
//                   QString::number(end_time) + ".csv");
//  if (peopleFile.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
//    std::cout << "> Saving People file... (size " << agents_.size() << ")"
//              << std::endl;
//    QTextStream streamP(&peopleFile);
//    streamP << "p,init_intersection,end_intersection,time_departure,traveled_"
//               "time(s),inqueue_time,slowdown_steps,"
//               "index,v,dv_dt,front_v,m2move,space,third_term,max_speed, "
//               "cum_distance,avg_v(m/s),status,lane_number,change_lane, eid, "
//               "located_eid\n";
//
//    for (int p = 0; p < agents_.size(); p++) {
//      streamP << p;
//      streamP << "," << agents_[p].init_intersection;
//      streamP << "," << agents_[p].end_intersection;
//      streamP << "," << agents_[p].time_departure;
//      streamP << "," << agents_[p].num_steps;
//      streamP << "," << agents_[p].num_steps_in_queue;
//      streamP << "," << agents_[p].slow_down_steps;
//      streamP << "," << agents_[p].indexPathCurr;
//      streamP << "," << agents_[p].v;
//      streamP << "," << agents_[p].dv_dt;
//      streamP << "," << agents_[p].front_speed;
//      streamP << "," << agents_[p].m2move;
//      streamP << "," << agents_[p].s;
//      streamP << "," << agents_[p].thirdTerm;
//      streamP << "," << agents_[p].max_speed;
//      streamP << "," << agents_[p].cum_length;
//      streamP << "," << (agents_[p].cum_v / agents_[p].num_steps);
//      streamP << "," << agents_[p].active;
//      streamP << "," << agents_[p].lane;
//      streamP << "," << agents_[p].num_lane_change;
//      streamP << "," << agents_[p].edge_ptr;
//      streamP << "," << agents_[p].located_eid;
//
//      streamP << "\n";
//    }
//
//    peopleFile.close();
//    std::cout << "> Finished saving People file." << std::endl;
//  }
//}
//
// bool isLastEdgeOfPath(abm::graph::edge_id_t edgeInPath) {
//  return edgeInPath == -1;
//}
//
// void TrafficSimulator::writeRouteFile(
//    int numOfPass, const std::vector<abm::graph::edge_id_t> &paths_SP,
//    int start_time, int end_time) {
//  QFile routeFile(QString::fromStdString(save_path_) + "route" +
//                  QString::number(start_time) + "to" +
//                  QString::number(end_time) + ".csv");
//  if (routeFile.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
//    std::cout << "> Saving Route file..." << std::endl;
//    QHash<uint, uint> laneMapNumCount;
//    QTextStream streamR(&routeFile);
//    streamR << "p:route\n";
//    int lineIndex = 0;
//    int peopleIndex = 0;
//    streamR << lineIndex << ":[";
//    for (const abm::graph::edge_id_t &edgeInPath : paths_SP) {
//      if (isLastEdgeOfPath(edgeInPath)) {
//        streamR << "]\n";
//        lineIndex++;
//        if (peopleIndex != paths_SP.size() - 1) {
//          streamR << lineIndex << ":[";
//        }
//      } else {
//        streamR << edgeInPath << ",";
//      }
//      peopleIndex++;
//    }
//    routeFile.close();
//  }
//  std::cout << "> Finished saving Route file." << std::endl;
//}
//
// void TrafficSimulator::writeIndexPathVecFile(
//    int numOfPass, int start_time, int end_time,
//    const std::vector<uint> &indexPathVec) {
//  QFile indexPathVecFile(QString::fromStdString(save_path_) + "indexPathVec" +
//                         QString::number(start_time) + "to" +
//                         QString::number(end_time) + ".csv");
//  if (indexPathVecFile.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
//    std::cout << "> Saving indexPathVec (size " << indexPathVec.size() <<
//    ")..."
//              << std::endl;
//    QTextStream indexPathVecStream(&indexPathVecFile);
//    indexPathVecStream << "indexPathVec\n";
//
//    for (auto const &elemIndexPathVec : indexPathVec) {
//      indexPathVecStream << elemIndexPathVec << "\n";
//    }
//
//    indexPathVecFile.close();
//  }
//  std::cout << "> Finished saving indexPathVec..." << std::endl;
//}
//
// void TrafficSimulator::savePeopleAndRoutesSP(
//    int numOfPass, const std::shared_ptr<abm::Graph> &graph_,
//    const std::vector<abm::graph::edge_id_t> &paths_SP, int start_time,
//    int end_time) {
//  bool enableMultiThreading = true;
//  const bool saveToFile = true;
//
//  if (!saveToFile) {
//    return;
//  }
//
//  writePeopleFile(numOfPass, graph_, start_time, end_time, agents_,
//  deltaTime_); writeRouteFile(numOfPass, paths_SP, start_time, end_time);
//  writeIndexPathVecFile(numOfPass, start_time, end_time,
//                        lanemap_.indexPathVec());
//
//  //  if (enableMultiThreading) {
//  //    //    std::cout << "Saving People, Route and IndexPathVec files..." <<
//  //    //    std::endl; std::thread
//  //    //    threadWritePeopleFile(&TrafficSimulator::writePeopleFile,
//  //    numOfPass,
//  //    //    graph_,
//  //    //                                      start_time, end_time, agents_,
//  //    //                                      deltaTime_);
//  //    ////    std::thread
//  //    threadWriteRouteFile(&TrafficSimulator::writeRouteFile,
//  //    ///numOfPass, paths_SP, / start_time,
//  //    ///end_time); /    auto indexPathVec = lanemap_.indexPathVec(); /
//  //    ///std::thread
//  // ///threadWriteIndexPathVecFile(&TrafficSimulator::writeIndexPathVecFile,
//  //    ///numOfPass, /                                            start_time,
//  //    ///end_time, indexPathVec);
//  //    //    threadWritePeopleFile.join();
//  //    ////    threadWriteRouteFile.join();
//  //    ////    threadWriteIndexPathVecFile.join();
//  //    //    std::cout << "Finished saving People, Route and IndexPathVec
//  //    files."
//  //    //              << std::endl;
//  //  } else {
//  //    writePeopleFile(numOfPass, graph_, start_time, end_time, agents_,
//  //                    deltaTime_);
//  //    writeRouteFile(numOfPass, paths_SP, start_time, end_time);
//  //        writeIndexPathVecFile(numOfPass,
//  //        ////                                            start_time,
//  //        end_time,
//  //        ///indexPathVec);
//  //  }
//}
//
// void TrafficSimulator::save_edges(
//    const std::vector<std::vector<unsigned>> &edge_upstream_count,
//    const std::vector<std::vector<unsigned>> &edge_downstream_count) {
//
//  std::ofstream upFile(save_path_ + "upstream_count.csv");
//  std::ofstream downFile(save_path_ + "downstream_count.csv");
//
//  for (const auto &et : edge_upstream_count) {
//    for (const auto &e : et) {
//      upFile << e << ",";
//    }
//    upFile << "\n";
//  }
//
//  for (const auto &et : edge_downstream_count) {
//    for (const auto &e : et) {
//      downFile << e << ",";
//    }
//    downFile << "\n";
//  }
//}
//
// void TrafficSimulator::save_intersection(
//    const std::vector<std::vector<unsigned>> &intersection_count) {
//
//  std::ofstream file(save_path_ + "intersection_count.csv");
//
//  for (const auto &et : intersection_count) {
//    for (const auto &e : et) {
//      file << e << ",";
//    }
//    file << "\n";
//  }
//}

} // namespace LC
