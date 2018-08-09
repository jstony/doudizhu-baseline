#pragma once

#include <vector>

#include <iostream>
#include <fstream>

#include "player.h"
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <random>

using namespace std;


enum class StateId {
	NORMAL = 0,
	FINISHED = 1
};

struct HashNode {
	unordered_map<HashNode*, int> _cnt_map;
	CardGroup _cg;
	~HashNode()
	{
		for (const auto &p : _cnt_map)
		{
			delete p.first;
		}
	}
};

// TODO: use compact data type
class State {
public:
	CardGroup _last_group, _cache_cg;
	StateId _id;
	vector<Player*> _players;
	int _current_idx, _current_controller, _winner, _target_idx;
	uint8_t _remain_len;
	bool _single;

	State(const State &s);
	State(const Env &env);
	~State();

	vector<vector<CardGroup>::iterator> get_action_space(bool all = false) const;
};


class Edge;

class Node {
public:
	State *st = nullptr;
	vector<vector<CardGroup>::iterator> actions;
	Edge *src = nullptr;
	vector<Edge*> edges;
	std::mutex mu;

	Node(Edge *src, State*st, vector<float> priors = vector<float>());

	~Node();

	Edge *choose(float c = sqrtf(2.f));
};


class Edge {
public:
	vector<CardGroup>::iterator action;
	int n = 0;
	float w = 0.f;
	float q = 0.f;
	bool terminiated = false;
	std::shared_timed_mutex mu;
	float r = 0.f;
	float p = 0.f;
	Node *src = nullptr;
	Node *dest = nullptr;

	Edge(Node *src, const vector<CardGroup>::iterator &action, float prior);

	~Edge();
};


class MCTree {
public:
	Node *root = nullptr;
	int idx = -1;
	int counter = 0;
	float c = 0;
	std::mutex counter_mu;

	MCTree(State*, float c = sqrtf(2.f));

	~MCTree();

	void search(int n_threads, int n);
	void search_thread(mt19937 *generator);
	Node *explore(Node *node, float&val, mt19937 &generator);
	void backup(Node *node, float val);
	float rollout(Node *node, mt19937 &generator);
	void predict(HashNode *node);
};

void step_ref(State &s, const vector<CardGroup>::iterator &a);
State* step(const State& s, const vector<CardGroup>::iterator &a);