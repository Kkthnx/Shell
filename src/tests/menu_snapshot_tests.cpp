#include "MenuId.h"
#include "MenuSnapshot.h"

#define NOMINMAX
#include "System/IO/DeleteTree.h"

#include <cstdlib>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{
	int failures = 0;

	bool check(bool value, const char *expression, int line)
	{
		if(!value)
		{
			std::cerr << "FAIL line " << line << ": " << expression << '\n';
			++failures;
		}
		return value;
	}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)
#define REQUIRE(expression) do { if(!CHECK(expression)) return; } while(0)

	struct Path
	{
		std::string text;

		Path() = default;
		explicit Path(std::string value) : text(std::move(value)) {}

		bool empty() const { return text.empty(); }
		uint32_t hash() const { return static_cast<uint32_t>(std::hash<std::string>{}(text)); }
	};

	struct Node
	{
		Path path;
		Node *parent = nullptr;
		std::vector<Node *> items;
		bool moved = false;
		int id = 0;
		inline static int live = 0;

		Node() { ++live; }

		~Node()
		{
			for(auto *child : items)
				delete child;
			--live;
		}
	};

	struct LiveGuard
	{
		int before = Node::live;

		~LiveGuard()
		{
			CHECK(Node::live == before);
		}
	};

	Node *add(Node *parent, std::string path, int id, bool moved = false)
	{
		auto *node = new Node;
		node->path = Path(std::move(path));
		node->parent = parent;
		node->id = id;
		node->moved = moved;
		parent->items.push_back(node);
		return node;
	}

	bool has_child(const Node *parent, const Node *child)
	{
		return std::find(parent->items.begin(), parent->items.end(), child) != parent->items.end();
	}

	std::vector<int> ids_of(const std::vector<Node *> &nodes)
	{
		std::vector<int> ids;
		for(auto *node : nodes)
		{
			if(node)
				ids.push_back(node->id);
		}

		std::sort(ids.begin(), ids.end());
		return ids;
	}

	bool same_ids(const std::vector<int> &actual, std::initializer_list<int> expected)
	{
		return actual == std::vector<int>(expected);
	}

	namespace ms = Nilesoft::Shell::MenuSnapshot;

	void test_nested_moved_item_reattaches_to_rebuilt_parent()
	{
		LiveGuard guard;
		Node root;
		auto *old_parent = add(&root, "A", 1);
		add(old_parent, "A/native", 2);
		auto *moved = add(old_parent, "A/moved", 3, true);
		add(&root, "B", 4);

		Node *new_parent = nullptr;
		Node *new_sibling = nullptr;
		std::vector<int> doomed_ids;
		bool rebuilt = false;

		auto result = ms::refresh_preserving_moved(&root,
			[&]
			{
				rebuilt = true;
				new_parent = add(&root, "A", 5);
				add(new_parent, "A/native2", 6);
				new_sibling = add(&root, "B", 7);
			},
			[&](const std::vector<Node *> &nodes)
			{
				CHECK(!rebuilt);
				doomed_ids = ids_of(nodes);
			});

		CHECK(rebuilt);
		REQUIRE(result.preserved.size() == 1u);
		CHECK(result.preserved[0].item == moved);
		CHECK(result.preserved[0].parent_path.text == "A");
		REQUIRE(new_parent != nullptr);
		REQUIRE(new_sibling != nullptr);
		CHECK(moved->parent == new_parent);
		CHECK(has_child(new_parent, moved));
		CHECK(!has_child(&root, moved));
		CHECK(root.items.size() == 2u);
		CHECK(root.items[0] == new_parent);
		CHECK(root.items[1] == new_sibling);
		CHECK(same_ids(doomed_ids, { 1, 2, 4 }));
	}

	void test_nested_moved_subtree_is_preserved_once()
	{
		LiveGuard guard;
		Node root;
		auto *old_parent = add(&root, "A", 1);
		auto *moved_parent = add(old_parent, "A/moved", 2, true);
		auto *moved_child = add(moved_parent, "A/moved/child", 3, true);
		add(&root, "B", 4);

		Node *new_parent = nullptr;
		Node *new_sibling = nullptr;
		std::vector<int> doomed_ids;
		bool rebuilt = false;

		auto result = ms::refresh_preserving_moved(&root,
			[&]
			{
				rebuilt = true;
				new_parent = add(&root, "A", 5);
				add(new_parent, "A/native", 6);
				new_sibling = add(&root, "B", 7);
			},
			[&](const std::vector<Node *> &nodes)
			{
				CHECK(!rebuilt);
				doomed_ids = ids_of(nodes);
			});

		CHECK(rebuilt);
		REQUIRE(result.preserved.size() == 1u);
		CHECK(result.preserved[0].item == moved_parent);
		CHECK(result.preserved[0].parent_path.text == "A");
		REQUIRE(new_parent != nullptr);
		REQUIRE(new_sibling != nullptr);
		CHECK(moved_parent->parent == new_parent);
		CHECK(moved_child->parent == moved_parent);
		CHECK(has_child(moved_parent, moved_child));
		CHECK(has_child(new_parent, moved_parent));
		CHECK(!has_child(&root, moved_parent));
		CHECK(root.items.size() == 2u);
		CHECK(root.items[0] == new_parent);
		CHECK(root.items[1] == new_sibling);
		CHECK(same_ids(doomed_ids, { 1, 4 }));
	}

	void test_moved_item_falls_back_when_parent_disappears()
	{
		LiveGuard guard;
		Node root;
		auto *old_parent = add(&root, "Gone", 1);
		auto *moved = add(old_parent, "Gone/moved", 2, true);

		Node *kept = nullptr;
		std::vector<int> doomed_ids;

		auto result = ms::refresh_preserving_moved(&root,
			[&]
			{
				kept = add(&root, "Keep", 3);
			},
			[&](const std::vector<Node *> &nodes)
			{
				doomed_ids = ids_of(nodes);
			});

		REQUIRE(result.preserved.size() == 1u);
		CHECK(result.preserved[0].item == moved);
		CHECK(result.preserved[0].parent_path.text == "Gone");
		REQUIRE(kept != nullptr);
		CHECK(moved->parent == &root);
		CHECK(has_child(&root, moved));
		CHECK(root.items.size() == 2u);
		CHECK(root.items[0] == kept);
		CHECK(root.items[1] == moved);
		CHECK(same_ids(doomed_ids, { 1 }));
	}

	void test_cleanup_runs_before_rebuild_for_every_native_node()
	{
		LiveGuard guard;
		Node root;
		auto *a = add(&root, "A", 1);
		add(a, "A/one", 2);
		add(a, "A/two", 3);
		add(&root, "B", 4);

		std::vector<std::string> events;
		std::vector<int> doomed_ids;

		auto result = ms::refresh_preserving_moved(&root,
			[&]
			{
				events.push_back("rebuild");
				add(&root, "A", 5);
				add(&root, "B", 6);
			},
			[&](const std::vector<Node *> &nodes)
			{
				events.push_back("cleanup");
				doomed_ids = ids_of(nodes);
			});

		REQUIRE(result.preserved.empty());
		REQUIRE(events.size() == 2u);
		CHECK(events[0] == "cleanup");
		CHECK(events[1] == "rebuild");
		CHECK(same_ids(doomed_ids, { 1, 2, 3, 4 }));
	}

	void test_top_level_moved_item_survives_rebuild()
	{
		LiveGuard guard;
		Node root;
		auto *moved = add(&root, "Moved", 1, true);
		add(&root, "Native", 2);

		Node *rebuilt_native = nullptr;
		std::vector<int> doomed_ids;

		auto result = ms::refresh_preserving_moved(&root,
			[&]
			{
				rebuilt_native = add(&root, "Native", 3);
			},
			[&](const std::vector<Node *> &nodes)
			{
				doomed_ids = ids_of(nodes);
			});

		REQUIRE(result.preserved.size() == 1u);
		CHECK(result.preserved[0].item == moved);
		CHECK(result.preserved[0].parent_is_menu);
		CHECK(result.preserved[0].parent_path.empty());
		REQUIRE(rebuilt_native != nullptr);
		CHECK(moved->parent == &root);
		CHECK(has_child(&root, moved));
		CHECK(root.items.size() == 2u);
		CHECK(root.items[0] == rebuilt_native);
		CHECK(root.items[1] == moved);
		CHECK(same_ids(doomed_ids, { 2 }));
	}

	void test_duplicate_popup_ids_are_allocated_unique_internal_ids()
	{
		uint32_t next_id = 0x0fffffff;
		std::vector<uint32_t> ids;
		for(int i = 0; i < 5; ++i)
			ids.push_back(Nilesoft::Shell::MenuId::allocate(13, true, next_id));

		for(size_t i = 0; i < ids.size(); ++i)
		{
			for(size_t j = i + 1; j < ids.size(); ++j)
				CHECK(ids[i] != ids[j]);
		}

		CHECK(ids.front() == 0x0fffffff);
		CHECK(next_id == 0x0fffffff + 5);

		CHECK(Nilesoft::Shell::MenuId::allocate(13, false, next_id) == 13);
		CHECK(Nilesoft::Shell::MenuId::allocate(0, false, next_id) == 0x0fffffff + 5);
		CHECK(next_id == 0x0fffffff + 6);
	}

	void test_large_snapshot_rebuild_completes()
	{
		LiveGuard guard;
		Node root;
		constexpr int native_count = 10000;

		for(int i = 0; i < native_count; ++i)
		{
			auto *parent = add(&root, "Native" + std::to_string(i), i + 1);
			add(parent, "Native" + std::to_string(i) + "/child", i + 100000);
			if(i == 0)
				add(parent, "Native0/moved", 2, true);
		}

		auto *moved = root.items[0]->items.back();
		size_t doomed_count = 0;
		auto start = std::chrono::steady_clock::now();

		auto result = ms::refresh_preserving_moved(&root,
			[&]
			{
				for(int i = 0; i < native_count; ++i)
				{
					auto *parent = add(&root, "Native" + std::to_string(i), i + 200000);
					add(parent, "Native" + std::to_string(i) + "/child", i + 300000);
				}
			},
			[&](const std::vector<Node *> &nodes)
			{
				doomed_count = nodes.size();
			});

		auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
		std::cout << "large snapshot (" << (native_count * 2) << " native nodes): "
				  << elapsed.count() << " ms\n";

		REQUIRE(result.preserved.size() == 1u);
		CHECK(result.preserved[0].item == moved);
		REQUIRE(root.items.size() == static_cast<size_t>(native_count));

		auto *rebuilt = root.items[0];
		REQUIRE(rebuilt != nullptr);
		CHECK(rebuilt->path.text == "Native0");
		CHECK(moved->parent == rebuilt);
		CHECK(has_child(rebuilt, moved));
		CHECK(doomed_count == static_cast<size_t>(native_count * 2));
	}
}

namespace
{
	using Nilesoft::IO::DeleteTree;

	bool exists(const std::wstring &path)
	{
		return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
	}

	void write_file(const std::wstring &path)
	{
		auto file = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
		if(file != INVALID_HANDLE_VALUE)
		{
			DWORD written = 0;
			::WriteFile(file, "x", 1, &written, nullptr);
			::CloseHandle(file);
		}
	}

	// A fresh directory under %TEMP% for one test.
	std::wstring make_root(const wchar_t *name)
	{
		wchar_t temp[MAX_PATH]{};
		::GetTempPathW(MAX_PATH, temp);
		std::wstring root = std::wstring(temp) + L"shell_tests_" + name + L"_" +
			std::to_wstring(::GetCurrentProcessId());
		DeleteTree(root);
		::CreateDirectoryW(root.c_str(), nullptr);
		return root;
	}

	void test_delete_tree_removes_nested_contents()
	{
		auto root = make_root(L"nested");
		REQUIRE(exists(root));

		::CreateDirectoryW((root + L"\\a").c_str(), nullptr);
		::CreateDirectoryW((root + L"\\a\\b").c_str(), nullptr);
		write_file(root + L"\\top.txt");
		write_file(root + L"\\a\\b\\deep.txt");
		write_file(root + L"\\a\\readonly.txt");
		::SetFileAttributesW((root + L"\\a\\readonly.txt").c_str(), FILE_ATTRIBUTE_READONLY);
		::SetFileAttributesW((root + L"\\a\\b").c_str(), FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY);

		CHECK(DeleteTree(root));
		CHECK(!exists(root));
	}

	void test_delete_tree_keeps_junction_target()
	{
		auto root = make_root(L"junction");
		auto outside = make_root(L"junction_target");
		write_file(outside + L"\\keep.txt");

		auto link = root + L"\\link";
		auto command = L"cmd /c mklink /J \"" + link + L"\" \"" + outside + L"\" >nul";
		REQUIRE(_wsystem(command.c_str()) == 0);
		REQUIRE(exists(link + L"\\keep.txt"));

		CHECK(DeleteTree(root));
		CHECK(!exists(root));
		CHECK(exists(outside + L"\\keep.txt"));

		DeleteTree(outside);
	}

	void test_delete_tree_handles_deep_tree()
	{
		auto root = make_root(L"deep");
		auto path = root;
		int depth = 0;
		while(path.size() + 2 < 240 && ::CreateDirectoryW((path + L"\\d").c_str(), nullptr))
		{
			path += L"\\d";
			depth++;
		}
		write_file(path + L"\\leaf.txt");
		CHECK(depth > 50);

		CHECK(DeleteTree(root));
		CHECK(!exists(root));
	}

	void test_delete_tree_single_file_and_missing_path()
	{
		auto root = make_root(L"file");
		auto file = root + L"\\only.txt";
		write_file(file);

		CHECK(DeleteTree(file));
		CHECK(!exists(file));
		CHECK(!DeleteTree(file));

		DeleteTree(root);
	}
}

int main()
{
	test_delete_tree_removes_nested_contents();
	test_delete_tree_keeps_junction_target();
	test_delete_tree_handles_deep_tree();
	test_delete_tree_single_file_and_missing_path();

	test_nested_moved_item_reattaches_to_rebuilt_parent();
	test_nested_moved_subtree_is_preserved_once();
	test_moved_item_falls_back_when_parent_disappears();
	test_cleanup_runs_before_rebuild_for_every_native_node();
	test_top_level_moved_item_survives_rebuild();
	test_duplicate_popup_ids_are_allocated_unique_internal_ids();
	test_large_snapshot_rebuild_completes();

	if(failures)
	{
		std::cerr << failures << " check(s) failed\n";
		return 1;
	}

	std::cout << "menu snapshot tests: all checks passed\n";
	return 0;
}
