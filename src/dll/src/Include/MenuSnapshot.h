#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Nilesoft
{
	namespace Shell
	{
		namespace MenuSnapshot
		{
			template<typename Node>
			concept NodeLike = requires(Node &node)
			{
				node.items;
				node.parent;
				node.moved;
				node.path;
				node.path.hash();
				node.path.empty();
			};

			template<NodeLike Node>
			struct PreservedItem
			{
				using Path = std::decay_t<decltype(std::declval<Node &>().path)>;

				Node *item{};
				bool parent_is_menu{};
				Path parent_path;
			};

			template<NodeLike Node>
			struct RefreshResult
			{
				std::vector<PreservedItem<Node>> preserved;
			};

			// Rebuild a native snapshot while preserving the subtree of every
			// top-most node marked as moved. Node ownership stays with the tree:
			// deleting a child must recursively delete its descendants.
			//
			// before_delete is called with every old native node before the old
			// snapshot is freed, so callers can purge external pointers first.
			// rebuild must repopulate menu->items with the new native snapshot.
			template<NodeLike Node, typename Rebuild, typename BeforeDelete>
			RefreshResult<Node> refresh_preserving_moved(
				Node *menu,
				Rebuild &&rebuild,
				BeforeDelete &&before_delete)
			{
				using Path = typename PreservedItem<Node>::Path;

				struct PendingRoot
				{
					Node *item{};
					Node *parent{};
					bool parent_is_menu{};
					Path parent_path;
				};

				RefreshResult<Node> result;
				if(!menu)
					return result;

				std::vector<PendingRoot> roots;
				std::vector<Node *> pending;

				// Collect the top-most moved nodes in their original order. A moved
				// node's whole subtree is preserved, including nested moved nodes.
				for(auto it = menu->items.rbegin(); it != menu->items.rend(); ++it)
					pending.push_back(*it);

				while(!pending.empty())
				{
					auto node = pending.back();
					pending.pop_back();
					if(!node)
						continue;

					if(node->moved)
					{
						PendingRoot root;
						root.item = node;
						root.parent = node->parent;
						root.parent_is_menu = node->parent == menu;
						if(node->parent && !root.parent_is_menu)
							root.parent_path = node->parent->path;
						roots.push_back(std::move(root));
						continue;
					}

					for(auto it = node->items.rbegin(); it != node->items.rend(); ++it)
						pending.push_back(*it);
				}

				// Detach preserved items before the old native snapshot is deleted.
				for(auto &root : roots)
				{
					if(!root.parent)
						continue;

					auto &siblings = root.parent->items;
					auto it = std::find(siblings.begin(), siblings.end(), root.item);
					if(it != siblings.end())
						siblings.erase(it);
				}

				// Everything left is a native node that will be replaced.
				std::vector<Node *> doomed;
				pending.assign(menu->items.begin(), menu->items.end());
				while(!pending.empty())
				{
					auto node = pending.back();
					pending.pop_back();
					if(!node)
						continue;

					doomed.push_back(node);
					for(auto child : node->items)
						pending.push_back(child);
				}

				if(!doomed.empty())
					before_delete(doomed);

				for(auto child : menu->items)
					delete child;
				menu->items.clear();

				rebuild();

				// Map the rebuilt native tree by path so preserved items can be
				// reattached to the replacement of their former parent.
				std::unordered_map<uint32_t, Node *> rebuilt;
				pending.assign(menu->items.begin(), menu->items.end());
				while(!pending.empty())
				{
					auto node = pending.back();
					pending.pop_back();
					if(!node)
						continue;

					rebuilt[node->path.hash()] = node;
					for(auto child : node->items)
						pending.push_back(child);
				}

				for(auto &root : roots)
				{
					auto parent = menu;
					if(!root.parent_is_menu && !root.parent_path.empty())
					{
						auto it = rebuilt.find(root.parent_path.hash());
						if(it != rebuilt.end() && it->second)
							parent = it->second;
					}

					root.item->parent = parent;
					parent->items.push_back(root.item);

					PreservedItem<Node> preserved;
					preserved.item = root.item;
					preserved.parent_is_menu = root.parent_is_menu;
					preserved.parent_path = root.parent_path;
					result.preserved.push_back(std::move(preserved));
				}

				return result;
			}
		}
	}
}
