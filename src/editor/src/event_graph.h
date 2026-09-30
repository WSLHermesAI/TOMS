#pragma once
// event_graph.h -- the event editor's model, with no Qt in it on purpose.
//
// Why it is separate from the view: this is the part with rules (which pools exist, which floors roll an
// event, which text keys are missing, which events are orphaned), and rules belong in something a headless
// test can exercise. The QGraphicsView above it only positions and draws what this produces -- and because
// there is no Qt here, it compiles and is checked on every platform, including the ones where the editor
// itself cannot be built.
//
// What the relationships actually are (verified against the data, not assumed):
//   assets/data/events/pool_*.json   -> { "_comment": [...], "events": [ {eventId, kind, text} ] }
//   assets/data/story/floors/F*.json -> { "events": [ "<eventId>", ... ], ... }  (FloorTable reads this)
//   assets/data/text.json            -> { "strings": { "<key>": "..." } }
// so the edges are: pool -> event (membership), floor -> event (which floors can roll it), and
// event -> text key (localisation). The three failure modes the validator also guards become visible:
// an event no floor references, a floor referencing an id that does not exist, and a text key with no string.

#include <map>
#include <set>
#include <string>
#include <vector>

namespace toms::editor {

enum class NodeKind { Pool, Event, Floor, TextKey };

struct GraphNode {
    NodeKind    kind = NodeKind::Event;
    std::string id;        // eventId, "pool_common", "F01", or the text key
    std::string label;     // what to draw (kind / act for events)
    int         floorSeq = 0;   // floors only (1..70), for filtering and ordering
};

struct GraphEdge {
    std::string from, to;       // node ids
    enum class Kind { PoolEvent, FloorEvent, EventText } kind = Kind::PoolEvent;
};

struct GraphProblem {
    enum class Kind { OrphanEvent, MissingEventId, MissingTextKey } kind = Kind::OrphanEvent;
    std::string id;             // the offending id
    std::string detail;         // where it was found (file/floor), for a readable message
};

struct EventGraph {
    std::vector<GraphNode>    nodes;
    std::vector<GraphEdge>    edges;
    std::vector<GraphProblem> problems;

    const GraphNode* find(const std::string& id) const;
    int  countOf(NodeKind k) const;
};

// Reads the three file kinds above. `root` is the project root (the folder containing assets/).
// Missing files are skipped rather than fatal: a graph of whatever exists beats no editor at all.
EventGraph loadEventGraph(const std::string& root);

}  // namespace toms::editor
