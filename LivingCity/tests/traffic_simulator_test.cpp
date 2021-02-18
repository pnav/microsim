#include "catch.hpp"
#include "traffic_simulator.h"
#include "b18CommandLineVersion.h"

using namespace LC;
TEST_CASE("CHECK SIMULATOR", "[SIMULATOR]") {
    const IDMParameters simParameters;
    ClientGeometry cg;
    TrafficSimulator simulator(&cg.roadGraph, simParameters);

    SECTION("Check loaded agents") {
        std::string networkPath = "../tests/test_data/";
        std::string odFileName = "od.csv";
        auto network = std::make_shared<LC::Network>(networkPath, odFileName);

        simulator.load_agents(network);

        auto agents = simulator.agents();
        REQUIRE(agents.size() == 4);

        auto a0 = agents.at(0);
        REQUIRE(a0.init_intersection == 0);
        REQUIRE(a0.end_intersection == 4);
        REQUIRE(a0.time_departure == 0);

        auto a1 = agents.at(1);
        REQUIRE(a1.init_intersection == 1);
        REQUIRE(a1.end_intersection == 4);
        REQUIRE(a1.time_departure == 300);

        auto a2 = agents.at(2);
        REQUIRE(a2.init_intersection == 1);
        REQUIRE(a2.end_intersection == 4);
        REQUIRE(a2.time_departure == 350);

        auto a3 = agents.at(3);
        REQUIRE(a3.init_intersection == 0);
        REQUIRE(a3.end_intersection == 4);
        REQUIRE(a3.time_departure == 400);

    }
}
