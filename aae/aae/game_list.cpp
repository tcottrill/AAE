/*============================================================================
  game_list.cpp  –  see game_list.h for overview & license
  ============================================================================*/
#include "aae_mame_driver.h"
#include "game_list.h"
#include <algorithm>
#include <cassert>

  // ---------------------------------------------------------------------------
  // Constructors
  // ---------------------------------------------------------------------------
GameList::GameList() : head_(nullptr) {}

GameList::GameList(const AAEDriver* drivers) : head_(nullptr)
{
	build(drivers);
}

GameList::GameList(const std::vector<const AAEDriver*>& reg) : head_(nullptr)
{
	build(reg);
}
// ---------------------------------------------------------------------------
// Build from AAEDriver[]
// ---------------------------------------------------------------------------
void GameList::build(const AAEDriver* drivers)
{
	nodes_.clear();
	head_ = nullptr;
	if (!drivers) return;

	//----------------------------------------------------------------------
	// Loop through driver table, starting at index 1 to skip the GUI entry.
	//----------------------------------------------------------------------
	for (int idx = 1; drivers[idx].name != nullptr; ++idx)
	{
		GameNode node;
		node.gameNum = idx;

		//--------------------------------------------------------------
		// 1) Copy the short name and capitalize its first letter.
		//--------------------------------------------------------------
		std::string shortName(drivers[idx].name ? drivers[idx].name : "");
		if (!shortName.empty())
		{
			unsigned char ch = static_cast<unsigned char>(shortName[0]);
			if (std::islower(ch))
				shortName[0] = static_cast<char>(std::toupper(ch));
		}

		//--------------------------------------------------------------
		// 2) Populate node fields.
		//--------------------------------------------------------------
		node.displayName = shortName;                      // used for sorting/UI
		node.description = drivers[idx].desc               // long title
			? drivers[idx].desc
			: shortName;
		// node.extraOptions remains 0 (default constructor)

		nodes_.emplace_back(node);   // vector now holds a capitalized name
	}

	//----------------------------------------------------------------------
	// Nothing to sort/link if list is empty.
	//----------------------------------------------------------------------
	if (nodes_.empty()) return;

	//----------------------------------------------------------------------
	// 3) Alphabetically sort by displayName (already capitalized).
	//----------------------------------------------------------------------
	std::sort(nodes_.begin(), nodes_.end(), compareNodes);

	//----------------------------------------------------------------------
	// 4) Link nodes into a circular doubly-linked list.
	//----------------------------------------------------------------------
	linkCircular();
	head_ = &nodes_.front();
}

void GameList::build(const std::vector<const AAEDriver*>& reg)
{
	nodes_.clear();
	head_ = nullptr;
	if (reg.empty()) return;

	for (std::size_t idx = 0; idx < reg.size(); ++idx) {
		const AAEDriver* d = reg[idx];
		if (!d || !d->name) continue;
		GameNode node;
		node.gameNum = static_cast<int>(idx);  // preserve original index
		std::string shortName(d->name);
		if (!shortName.empty()) {
			unsigned char ch = static_cast<unsigned char>(shortName[0]);
			if (std::islower(ch)) shortName[0] = static_cast<char>(std::toupper(ch));
		}
		node.displayName = shortName;
		node.description = d->desc ? d->desc : shortName;
		nodes_.emplace_back(node);
	}
	if (nodes_.empty()) return;
	std::sort(nodes_.begin(), nodes_.end(), compareNodes);
	linkCircular();
	head_ = &nodes_.front();
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
bool GameList::compareNodes(const GameNode& a, const GameNode& b)
{
	return a.displayName < b.displayName;
}

// Case insensitive comparison code
/*
static bool compareNodes(const GameNode& a, const GameNode& b)
{
	const std::string& s1 = a.displayName;
	const std::string& s2 = b.displayName;

	for (std::size_t i = 0; i < s1.size() && i < s2.size(); ++i)
	{
		char c1 = std::tolower(static_cast<unsigned char>(s1[i]));
		char c2 = std::tolower(static_cast<unsigned char>(s2[i]));
		if (c1 != c2) return c1 < c2;
	}
	return s1.size() < s2.size();
}
*/

void GameList::linkCircular()
{
	std::size_t n = nodes_.size();
	for (std::size_t i = 0; i < n; ++i)
	{
		nodes_[i].next = &nodes_[(i + 1) % n];
		nodes_[i].prev = &nodes_[(i + n - 1) % n];
	}
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------
std::size_t GameList::size() const
{
	return nodes_.size();
}

GameNode* GameList::head() { return head_; }
const GameNode* GameList::head() const { return head_; }

const GameNode* GameList::findByGameNum(int gamenum) const
{
	for (std::size_t i = 0; i < nodes_.size(); ++i)
		if (nodes_[i].gameNum == gamenum)
			return &nodes_[i];
	return nullptr;
}

GameNode& GameList::operator[](std::size_t index)
{
	assert(index < nodes_.size());
	return nodes_[index];
}

const GameNode& GameList::operator[](std::size_t index) const
{
	assert(index < nodes_.size());
	return nodes_[index];
}

// ---------------------------------------------------------------------------
// indexOfGameNum – sorted index for given driver index, or -1
// ---------------------------------------------------------------------------
int GameList::indexOfGameNum(int gamenum) const
{
	for (std::size_t i = 0; i < nodes_.size(); ++i)
		if (nodes_[i].gameNum == gamenum)
			return static_cast<int>(i);
	return -1;
}