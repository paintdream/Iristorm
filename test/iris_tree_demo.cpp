#include "../src/iris_tree.h"
#include "../src/iris_common.inl"
#include <utility>
#include <vector>
#include <ctime>
#include <cstdio>
using namespace iris;

struct float3 {
	using type = float;
	static constexpr size_t size = 3;

	explicit float3(float xx = 0.0f, float yy = 0.0f, float zz = 0.0f) noexcept : x(xx), y(yy), z(zz) {}
	constexpr float operator [] (size_t index) const noexcept {
		return index == 0 ? x : index == 1 ? y : z;
	}

	float& operator [] (size_t index) noexcept {
		return index == 0 ? x : index == 1 ? y : z;
	}

	float x, y, z;
};

using box = std::pair<float3, float3>;

static bool overlap(const box& lhs, const box& rhs) noexcept {
	if (rhs.second.x < lhs.first.x || lhs.second.x < rhs.first.x)
		return false;

	if (rhs.second.y < lhs.first.y || lhs.second.y < rhs.first.y)
		return false;

	if (rhs.second.z < lhs.first.z || lhs.second.z < rhs.first.z)
		return false;

	return true;
}

static box build_box(const float3& first, const float3& second) noexcept {
	box b;
	b.first.x = std::min(first.x, second.x);
	b.first.y = std::min(first.y, second.y);
	b.first.z = std::min(first.z, second.z);
	b.second.x = std::max(first.x, second.x);
	b.second.y = std::max(first.y, second.y);
	b.second.z = std::max(first.z, second.z);

	return b;
}

struct sample_tree : iris_tree_t<box> {
	using base = iris_tree_t<box>;
	sample_tree() noexcept : iris_tree_t<box>(box(float3(0, 0, 0), float3(0, 0, 0)), 0) {}
	sample_tree(const box& b, uint8_t k) noexcept : iris_tree_t<box>(b, k) {}
};

static box build_box_randomly() noexcept {
	return build_box(float3((float)rand(), (float)rand(), (float)rand()), float3((float)rand(), (float)rand(), (float)rand()));
}

struct queryer {
	queryer() : count(0) {}
	size_t count;
	box bounding;
	bool operator () (const sample_tree::base& tree) noexcept {
		if (overlap(bounding, tree.get_key()))
			count++;
		return true;
	}
};

size_t fast_query(sample_tree*& root, const box& box) noexcept {
	queryer q;
	q.bounding = box;
	IRIS_ASSERT(root->get_parent() == nullptr);
	root->query<true>(box, q);

	return q.count;
}

struct random_select {
	bool operator () (sample_tree::base* left, sample_tree::base* right) noexcept {
		return rand() & 1;
	}
};

size_t linear_search(const sample_tree* root, std::vector<sample_tree>& nodes, const box& box) noexcept {
	size_t count = 0;
	for (size_t i = 0; i < nodes.size(); i++) {
		if (nodes[i].get_parent() != nullptr || &nodes[i] == root)
			count += overlap(nodes[i].get_key(), box);
	}

	return count;
}

// Degenerate (all-equal key) detach->re-key->reattach stress. This reproduces
// the engine's TransformSystem::SyncNodeSystem flow, which detaches a dirty
// node, re-keys its AABB in place, and reattaches it. The historical bug was
// using detach()'s return value as "the parent to reattach to": detach() only
// returns a non-null replacement root when the DETACHED node was the root, so
// a non-root node was detached and never reattached - it silently dropped out
// of the tree. The correct flow captures the tree root BEFORE detach, then
// reattaches to it. This test drives equal-key (degenerate) and distinct-key
// nodes through that flow and checks every node stays reachable and the query
// result still matches a brute-force linear scan.
struct always_true_select {
	explicit always_true_select(sample_tree::base*) noexcept {}
	bool operator () (sample_tree::base*, sample_tree::base*) noexcept { return true; }
};

struct ptr_cmp_select {
	sample_tree::base* self;
	explicit ptr_cmp_select(sample_tree::base* s) noexcept : self(s) {}
	bool operator () (sample_tree::base* left, sample_tree::base*) noexcept { return left < self; }
};

template <typename selector_factory_t>
static bool run_detach_reattach_case(size_t count, bool degenerate) {
	std::vector<sample_tree> nodes(count);
	for (size_t i = 0; i < count; i++) {
		if (degenerate) {
			nodes[i] = sample_tree(build_box(float3(0, 0, 0), float3(0, 0, 0)), i % 6);
		} else {
			nodes[i] = sample_tree(build_box(float3((float)i, (float)i, (float)i), float3((float)i + 1, (float)i + 1, (float)i + 1)), i % 6);
		}
	}

	sample_tree* root = &nodes[0];
	for (size_t j = 1; j < count; j++) {
		nodes[j].attach(root);
	}

	// Detach -> re-key -> reattach, node by node, exactly like SyncNodeSystem.
	for (size_t k = 1; k < count; k++) {
		sample_tree* to_detach = &nodes[k];

		// capture the tree root BEFORE detach
		sample_tree* tree_root = to_detach;
		while (tree_root->get_parent() != nullptr) {
			tree_root = static_cast<sample_tree*>(tree_root->get_parent());
		}

		selector_factory_t factory(to_detach);
		sample_tree* new_root = static_cast<sample_tree*>(to_detach->detach(factory));
		if (new_root != nullptr) {
			tree_root = new_root; // detached node was the root; tree root moved
			root = new_root;
		}

		// re-key in place (no-op here) and reattach to the captured root
		if (tree_root != to_detach) {
			to_detach->attach(tree_root);
		}
	}

	// every node must remain reachable from the root
	queryer q;
	q.bounding = build_box(float3(-1e9f, -1e9f, -1e9f), float3(1e9f, 1e9f, 1e9f));
	root->query<true>(q.bounding, q);
	if (q.count != count) {
		printf("degenerate detach/reattach: only %zu/%zu nodes reachable (lost nodes)\n", q.count, count);
		return false;
	}

	// query correctness vs linear scan
	for (size_t n = 0; n < 20; n++) {
		box b = build_box_randomly();
		size_t got = fast_query(root, b);
		size_t expected = linear_search(root, nodes, b);
		if (got != expected) {
			printf("degenerate detach/reattach: query %zu != linear %zu\n", got, expected);
			return false;
		}
	}

	return true;
}

int main(void) {
	static constexpr size_t length = 10;
	std::vector<sample_tree> nodes(length * 4096);
	srand(0);

	// initialize data
	for (size_t i = 0; i < nodes.size(); i++) {
		nodes[i] = sample_tree(build_box_randomly(), rand() % 6);
	}

	// link data
	// select root
	sample_tree* root = &nodes[rand() % nodes.size()];

	for (size_t j = 0; j < nodes.size(); j++) {
		if (root != &nodes[j]) {
			nodes[j].attach(root);
		}
	}

	// random detach data
	random_select random_selector;
	for (size_t k = 0; k < nodes.size() / 8; k++) {
		size_t index = rand() % nodes.size();
		sample_tree* to_detach = &nodes[index];
		sample_tree* new_root = static_cast<sample_tree*>(to_detach->detach(random_selector));
		if (new_root != nullptr) {
			root = new_root;
			if (k & 1) {
				to_detach->attach(root);
			}
		}
	}

	for (size_t j = 0; j < 2; j++) {
		// random select
		for (size_t n = 0; n < size_t(10 * length); n++) {
			box b = build_box_randomly();
			size_t search_count = linear_search(root, nodes, b);
			size_t query_count = fast_query(root, b);

			if (query_count != search_count) {
				printf("unmatched result, %d got, %d expected.\n", (int)query_count, (int)search_count);
				return -1;
			}
		}

		root->query<true>(build_box_randomly(), [](const sample_tree::base& tree) { return true; }, [](const box& key) { return true; });
		root = static_cast<sample_tree*>(root->optimize());
	}

	// Degenerate (equal-key) and distinct-key detach->re-key->reattach cases,
	// using the two selector strategies the engine actually uses
	// (NodeSystem::Detach = always-true; SyncNodeSystem = pointer compare).
	if (!run_detach_reattach_case<always_true_select>(10, true)) return -1;
	if (!run_detach_reattach_case<ptr_cmp_select>(10, true)) return -1;
	if (!run_detach_reattach_case<always_true_select>(100, true)) return -1;
	if (!run_detach_reattach_case<ptr_cmp_select>(100, true)) return -1;
	if (!run_detach_reattach_case<always_true_select>(10, false)) return -1;
	if (!run_detach_reattach_case<ptr_cmp_select>(10, false)) return -1;
	if (!run_detach_reattach_case<always_true_select>(100, false)) return -1;
	if (!run_detach_reattach_case<ptr_cmp_select>(100, false)) return -1;

	printf("degenerate detach/reattach: all cases passed\n");
	return 0;
}

