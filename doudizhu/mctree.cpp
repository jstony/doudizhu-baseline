#include "mctree.h"
#include <math.h>
#include <assert.h>
#include <unordered_set>
#include "env.h"


vector<mt19937> generators;

State::State(const State &s) {
	_last_group = s._last_group;
	_current_idx = s._current_idx;
	_current_controller = s._current_controller;
	_target_idx = s._target_idx;
	_winner = s._winner;
	_id = s._id;
	for (size_t i = 0; i < s._players.size(); i++)
	{
		_players.push_back(new Player(*s._players[i]));
	}
}

State::State(const Env &env) {
	_last_group = env._last_group;
	_current_idx = env._current_idx;
	_current_controller = env._current_controller;
	_winner = -1;
	_id = StateId::NORMAL;
	_target_idx = _current_idx;
	for (size_t i = 0; i < env._players.size(); i++)
	{
		_players.push_back(new Player(*env._players[i]));
	}
}

State::~State() {
	for (auto player : _players)
	{
		delete player;
	}
}

vector<vector<CardGroup>::iterator> State::get_action_space() const {
	return _players[_current_idx]->candidate(_last_group);
}



Node::Node(Edge *src, State *st, vector<float> priors) {
	this->st = st;
	this->actions = st->get_action_space();
	/*if (this->actions.size() > 300)
	{
		cout << "large branching factor" << endl;
	}*/
	if (priors.empty() && !this->actions.empty()) {
		priors = vector<float>(this->actions.size(), 1.f / this->actions.size());
	}
	this->src = src;
	for (size_t i = 0; i < this->actions.size(); i++) {
		this->edges.push_back(new Edge(this, this->actions[i], priors[i]));
	}
}

Node::~Node() {
	if (this->st) {
		delete this->st;
	}
	for (auto e : this->edges) {
		if (e) {
			delete e;
		}
	}
}

Edge::Edge(Node *src, const vector<CardGroup>::iterator &action, float prior) {
	this->action = action;
	this->p = prior;
	this->src = src;
}


Edge::~Edge() {
	if (this->dest) {
		delete this->dest;
	}
}


Edge* Node::choose(float c) {
	float sum = 0.f;
	size_t e_size = edges.size();
	for (size_t i = 0; i < edges.size(); i++) {
		sum += edges[i]->n;
	}

	float nsum_sqrt = sqrtf(sum);
	int best_idx = -1;
	float best = -100.f;
	for (size_t i = 0; i < e_size; i++) {
		float cand = edges[i]->q + (edges[i]->n == 0 ? 1000.f : c * nsum_sqrt / (edges[i]->n));
		if (cand > best) {
			best_idx = i;
			best = cand;
		}
	}
	return edges[best_idx];
}


MCTree::MCTree(State* st, float c) {
	this->root = new Node(nullptr, st);
	//cout << root->actions.size() << endl;
	this->counter = 0;
	this->c = c;
}


MCTree::~MCTree() {
	if (root) {
		delete root;
	}
}

void MCTree::search(int n_threads, int n) {
	if (generators.empty()) {
		for (int i = 0; i < n_threads; i++) {
			generators.push_back(mt19937(random_device{}()));
		}
	}
	counter = n;
	vector<thread> threads;
	for (int i = 0; i < n_threads; i++) {

		threads.push_back(std::move(std::thread(&MCTree::search_thread, this, &generators[i])));
	}
	for (auto& t : threads) {
		t.join();
	}
}

void MCTree::search_thread(mt19937 *generator) {
	while (true) {
		{
			std::lock_guard<std::mutex> lock(counter_mu);
			if (counter == 0) {
				break;
			}
			else {
				counter--;
			}
		}
		float val = 0.f;
		// cout << "explore" << endl;
		
		Node* leaf = explore(root, val, *generator);
		// cout << val << endl;
		backup(leaf, val);
	}
}


// TODO: change node lock to per 
Node* MCTree::explore(Node* node, float& val, mt19937 &generator) {
	std::unique_lock<std::mutex> lock(node->mu);
	auto edge = node->choose(this->c);
	if (edge->dest) {
		if (edge->terminiated) {
			val = edge->r;
			lock.unlock();
			return edge->dest;
		}
		else {
			lock.unlock();
			return explore(edge->dest, val, generator);
		}
	}
	else {
		// cout << node->st->idx << ": " << static_cast<int>(node->st->id) << ", ";
		// cout << node->st->get_action_space().size() << endl;
		State* sprime = step(*node->st, edge->action);
		edge->dest = new Node(edge, sprime);

		if (sprime->_id == StateId::FINISHED) {
			if (sprime->_winner == sprime->_target_idx)
			{
				edge->r = 1.f;
			} else {
				if (sprime->_target_idx + sprime->_winner == 3)
				{
					edge->r = 1.f;
				} else {
					edge->r = NEG_REWARD;
				}
			}
			edge->terminiated = true;
			val = edge->r;
			lock.unlock();
			return edge->dest;
		}
		lock.unlock();
		// cout << "rollout ";
		val = rollout(edge->dest, generator);
		// cout << val << endl;
		// if (val != 0) {
		//     cout << val << ", ";
		// }


		return edge->dest;
	}
}

void MCTree::backup(Node* node, float val) {
	while (node->src) {
		auto edge = node->src;
		{
			std::lock_guard<std::mutex> lock(edge->src->mu);
			edge->n++;
			edge->w += val;
			edge->q = edge->w / edge->n;
		}
		node = edge->src;
	}
}

float MCTree::rollout(Node* node, mt19937 &generator) {
	auto st = node->st;
	auto s = State(*st);
	while (s._id != StateId::FINISHED) {
		auto actions = s.get_action_space();
		step_ref(s, actions[generator() % actions.size()]);
	}
	float r = 0;
	// cout << st->winner << endl;
	if (s._winner == s._target_idx)
	{
		r = 1.f;
	}
	else {
		if (s._target_idx + s._winner == 3)
		{
			r = 1.f;
		}
		else {
			r = NEG_REWARD;
		}
	}
	return r;
}

vector<int> MCTree::predict() {
	vector<int> cnts;
	for (size_t i = 0; i < root->edges.size(); i++) {
		// cout << root->edges[i]->q << ", ";
		cnts.push_back(root->edges[i]->n);
	}
	// cout << endl;
	return cnts;
}

void step_ref(State &s, const vector<CardGroup>::iterator &a) {
	auto cg = *a;
	s._players[s._current_idx]->remove_cards(a->_cards);

	auto next_idx = (s._current_idx + 1) % 3;
	if (cg._category != Category::EMPTY)
	{
		s._current_controller = s._current_idx;
		s._last_group = cg;
		if (s._players[s._current_idx]->over())
		{
			s._winner = s._current_idx;
			s._id = StateId::FINISHED;
			return;
		}
	}
	if (next_idx == s._current_controller)
	{
		s._last_group = CardGroup({}, Category::EMPTY, 0);
	}
	s._current_idx = next_idx;
	return;
}

State* step(const State& s, const vector<CardGroup>::iterator &a) {
	auto cg = *a;
	State *sprime = new State(s);
	sprime->_players[sprime->_current_idx]->remove_cards(a->_cards);

	auto next_idx = (sprime->_current_idx + 1) % 3;
	if (cg._category != Category::EMPTY)
	{
		sprime->_current_controller = sprime->_current_idx;
		sprime->_last_group = cg;
		if (sprime->_players[sprime->_current_idx]->over())
		{
			sprime->_winner = sprime->_current_idx;
			sprime->_id = StateId::FINISHED;
			return sprime;
		}
	}
	if (next_idx == sprime->_current_controller)
	{
		sprime->_last_group = CardGroup({}, Category::EMPTY, 0);
	}
	sprime->_current_idx = next_idx;
	//cout << "stepped" << endl;
	return sprime;
}