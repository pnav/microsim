#ifndef LC_B18_TRAFFIC_LANEMAP_H
#define LC_B18_TRAFFIC_LANEMAP_H

#include "config.h"
#include "edge_data.h"
#include "traffic/sp/graph.h"
#include <QVector3D>
#include <set>

namespace LC {
//! Lanemap Class
//! \brief Class that convert the network graph into a lanemap for GPU
//! simulation. A lanemap is a long memory array contains all the lanes in the
//! graph
class Lanemap {
public:
  Lanemap() = default;
  ~Lanemap() = default;

  //! Lanemap Constructor
  //! \param[in] graph ptr to the network graph
  explicit Lanemap(const std::shared_ptr<abm::Graph> &graph) {
    create_edgesData_(graph);
    create_LaneMap_();
    create_intersections_(graph);
  };

  std::vector<EdgeData> &edgesData() { return edgesData_; }

  std::vector<uchar> &lanemap_array() { return laneMap_; }

  std::vector<IntersectionData> &intersections() {
    return intersections_;
  }

  const std::map<uint, abm::graph::edge_id_t> &mid2eid() const {
    return mid2eid_;
  }

  const std::map<abm::graph::edge_id_t, uint> &eid2mid() const {
    return eid2mid_;
  }

private:
  std::vector<uchar> laneMap_;
  std::vector<EdgeData> edgesData_;
  std::vector<IntersectionData> intersections_;

  //! A map that maps lanemap number to the corresponding edge object
  std::map<uint, abm::graph::edge_id_t> mid2eid_;
  //! A map that maps lanemap number to the corresponding edge object
  std::map<abm::graph::edge_id_t, uint> eid2mid_;

  //! Helper function that creates the edgeData. It parses the edges from the
  //! network and record the parsing sequence (id maps)
  void create_edgesData_(const std::shared_ptr<abm::Graph> &graph);
  //! Helper function that creates an empty lanemap (elements initialized as 1)
  void create_LaneMap_();
  //! Helper function that creates intersections
  void create_intersections_(const std::shared_ptr<abm::Graph> &graph);
};
} // namespace LC

#endif // LC_B18_TRAFFIC_LANEMAP_H
