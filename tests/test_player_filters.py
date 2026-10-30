"""Exercise the actual Lua extension in Lua 5.1 with an original-panel fixture."""
import json
from pathlib import Path
import sys
import unittest
from xml.etree import ElementTree

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from common import ROOT
import resources
from lupa.lua51 import LuaRuntime


FIXTURE = r'''
function element(id)
    local e = {id=id, attributes={}, listeners={}, scroll_top=217, value="keep query"}
    function e:SetAttribute(key, value)
        self.attributes[key] = value
        if key == "style" then self.local_display = value:match("display:%s*([^;]+)") end
    end
    -- RmlUI DataViewIf checks local display presence, not computed visibility.
    function e:apply_online(online)
        local visible = self.local_display == nil
        if visible ~= online then
            if online then self.local_display = nil else self.local_display = "none" end
        end
    end
    function e:computed_display()
        if self.local_display ~= nil then return self.local_display end
        if (" " .. (self.attributes.class or "") .. " "):find(" zh_filters_ready ", 1, true) then
            return filter_ready_display
        end
        return filter_default_display
    end
    function e:AddEventListener(event, listener)
        self.listeners[event] = self.listeners[event] or {}
        table.insert(self.listeners[event], listener)
    end
    function e:fire(event)
        for _, listener in ipairs(self.listeners[event] or {}) do listener({}, self) end
    end
    return e
end
document = element("document")
elements = {}
for _, id in ipairs({"zh_player_filters", "players", "toggle_history", "search",
                     "zh_filter_all", "zh_filter_friends", "zh_filter_company", "zh_filter_staff"}) do
    elements[id] = element(id)
end
for key, value in pairs(initial_filter_attributes) do
    elements.zh_player_filters:SetAttribute(key, value)
end
function document:GetElementById(id) return elements[id] end
function document:QuerySelector(query) return elements.search end
ready = 1
mode = 0
accepted = 1
native_loads = 0
history_clicks = 0
search_clicks = 0
mode_calls = {}
PlayerPanel = {}
function PlayerPanel.FiltersReady() return ready end
function PlayerPanel.GetFilter() return mode end
function PlayerPanel.SetFilter(name)
    table.insert(mode_calls, name)
    if accepted == 1 then
        for n, key in ipairs({"all", "friends", "company", "staff"}) do
            if key == name then mode = n - 1 end
        end
    end
    return accepted
end
function OnLoad(doc)
    native_loads = native_loads + 1
    elements.toggle_history:AddEventListener("click", function() history_clicks=history_clicks+1 end)
    elements.search:AddEventListener("change", function() search_clicks=search_clicks+1 end)
end
'''


class PlayerFilterLua(unittest.TestCase):
    def runtime(self, before=''):
        lua = LuaRuntime(unpack_returned_tuples=True)
        entries = json.loads((ROOT / 'profiles/resource-edits.json').read_text(encoding='utf-8'))['ui_entries']
        panel = next(row for row in entries if row['name'] == 'player_panel.rml')
        inserted = next(edit['text'] for edit in panel['edits'] if 'zh_player_filters' in edit.get('text', ''))
        styles = next(row['styles'] for row in entries if row['name'] == 'player_panel.rcss')
        displays = {style['selector']: style['properties'].get('display', 'block') for style in styles}
        lua.globals().initial_filter_attributes = lua.table_from(ElementTree.fromstring(inserted).attrib)
        lua.globals().filter_default_display = displays['#zh_player_filters']
        lua.globals().filter_ready_display = displays.get('#zh_player_filters.zh_filters_ready', 'block')
        lua.execute(FIXTURE)
        lua.execute(before)
        lua.execute((ROOT / 'ui/player_filters.lua').read_text(encoding='utf-8'))
        lua.execute('OnLoad(document)')
        return lua

    def display(self, lua, online=True):
        lua.globals().test_online = online
        lua.execute('elements.zh_player_filters:apply_online(test_online)')
        return lua.eval('elements.zh_player_filters:computed_display()')

    def test_missing_or_incompatible_native_module_keeps_original_list(self):
        for before in ('PlayerPanel.FiltersReady=nil', 'ready=0',
                       'function PlayerPanel.FiltersReady() error("unavailable") end'):
            with self.subTest(before=before):
                lua = self.runtime(before)
                self.assertEqual(lua.eval('native_loads'), 1)
                self.assertEqual(lua.eval('#mode_calls'), 0)
                self.assertEqual(self.display(lua), 'none')
                self.assertEqual(lua.eval('elements.players.scroll_top'), 217)

    def test_all_tabs_preserve_original_search_and_history_callbacks(self):
        lua = self.runtime()
        self.assertEqual(self.display(lua), 'flex')
        for name in ('friends', 'company', 'staff', 'all'):
            index = ('all', 'friends', 'company', 'staff').index(name)
            lua.execute('elements.players.scroll_top=450')
            lua.execute('elements.zh_filter_' + name + ':fire("click")')
            self.assertEqual(lua.eval('mode'), index)
            self.assertEqual(lua.eval('elements.players.scroll_top'), 0)
            self.assertIn('zh_filter_selected', lua.eval('elements.zh_filter_' + name + '.attributes.class'))
            self.assertEqual(lua.eval('elements.search.value'), 'keep query')
        lua.execute('elements.players.scroll_top=90; elements.toggle_history:fire("click")')
        self.assertEqual(lua.eval('history_clicks'), 1)
        self.assertEqual(lua.eval('mode'), 0)
        self.assertEqual(lua.eval('elements.players.scroll_top'), 0)
        lua.execute('elements.players.scroll_top=90; elements.search:fire("change")')
        self.assertEqual(lua.eval('search_clicks'), 1)
        self.assertEqual(lua.eval('elements.players.scroll_top'), 0)

    def test_rejected_native_request_preserves_scroll_and_hides_controls(self):
        lua = self.runtime()
        lua.execute('accepted=0; elements.players.scroll_top=319; elements.zh_filter_friends:fire("click")')
        self.assertEqual(lua.eval('elements.players.scroll_top'), 319)
        self.assertEqual(lua.eval('mode'), 0)
        self.assertEqual(self.display(lua), 'none')
        lua.execute('accepted=1; mode=2; document:fire("show")')
        self.assertIn('zh_filter_selected', lua.eval('elements.zh_filter_company.attributes.class'))

    def test_existing_native_mode_restored_when_document_opens(self):
        lua = self.runtime('mode=1')
        self.assertIn('zh_filter_selected', lua.eval('elements.zh_filter_friends.attributes.class'))
        self.assertEqual(lua.eval('elements.players.scroll_top'), 217)
        self.assertEqual(lua.eval('#mode_calls'), 0)
        lua.execute('ready=0; document:fire("show")')
        self.assertEqual(self.display(lua), 'none')

    def test_reopening_and_late_refresh_preserve_scroll_without_refiltering(self):
        lua = self.runtime('mode=2')
        lua.execute('elements.players.scroll_top=450; document:fire("show")')
        self.assertEqual(lua.eval('elements.players.scroll_top'), 450)
        lua.execute('PlayerPanel.ZHRefreshFilterTabs()')
        self.assertEqual(lua.eval('elements.players.scroll_top'), 450)
        self.assertEqual(lua.eval('#mode_calls'), 0)
        self.assertEqual(lua.eval('elements.search.value'), 'keep query')
        self.assertIn('zh_filter_selected', lua.eval('elements.zh_filter_company.attributes.class'))

    def test_clicking_active_filter_preserves_scroll(self):
        lua = self.runtime('mode=1')
        lua.execute('elements.players.scroll_top=450; elements.zh_filter_friends:fire("click")')
        self.assertEqual(lua.eval('elements.players.scroll_top'), 450)
        self.assertEqual(lua.eval('#mode_calls'), 0)

    def test_invalid_restored_filter_hides_controls_without_mutating_list(self):
        for getter in ('function() error("unavailable") end', 'function() return nil end',
                       'function() return "1" end', 'function() return 10 end'):
            with self.subTest(getter=getter):
                lua = self.runtime('PlayerPanel.GetFilter=' + getter)
                self.assertEqual(lua.eval('elements.players.scroll_top'), 217)
                self.assertEqual(lua.eval('#mode_calls'), 0)
                self.assertEqual(self.display(lua), 'none')

    def test_late_native_binding_reveals_existing_document(self):
        lua = self.runtime('saved_ready=PlayerPanel.FiltersReady; PlayerPanel.FiltersReady=nil')
        self.assertEqual(self.display(lua), 'none')
        lua.execute('PlayerPanel.FiltersReady=saved_ready; PlayerPanel.ZHRefreshFilterTabs()')
        self.assertEqual(self.display(lua), 'flex')
        self.assertEqual(lua.eval('native_loads'), 1)

    def test_offline_binding_stays_hidden_across_restore_and_reconnect(self):
        lua = self.runtime()
        self.assertEqual(self.display(lua, online=False), 'none')
        lua.execute('document:fire("show")')
        self.assertEqual(self.display(lua, online=False), 'none')
        self.assertEqual(self.display(lua, online=True), 'flex')
        self.assertEqual(self.display(lua, online=True), 'flex')

    def test_missing_module_stays_hidden_across_online_transitions(self):
        lua = self.runtime('PlayerPanel.FiltersReady=nil')
        for online in (False, True, True, False, True):
            self.assertEqual(self.display(lua, online=online), 'none')

    def test_late_binding_while_offline_waits_for_online(self):
        lua = self.runtime('saved_ready=PlayerPanel.FiltersReady; PlayerPanel.FiltersReady=nil')
        self.assertEqual(self.display(lua, online=False), 'none')
        lua.execute('PlayerPanel.FiltersReady=saved_ready; PlayerPanel.ZHRefreshFilterTabs()')
        self.assertEqual(lua.eval('elements.zh_player_filters:computed_display()'), 'none')
        self.assertEqual(self.display(lua, online=True), 'flex')

    def test_losing_native_readiness_stays_hidden_after_online_update(self):
        lua = self.runtime()
        self.assertEqual(self.display(lua), 'flex')
        lua.execute('ready=0; document:fire("show")')
        self.assertEqual(self.display(lua), 'none')
        self.assertEqual(self.display(lua, online=False), 'none')
        self.assertEqual(self.display(lua, online=True), 'none')

    def test_lua_patch_keeps_original_source_and_rejects_double_patch(self):
        source = (b'-- PlayerPanel.SearchPlayers PlayerPanel.SortPlayers PlayerPanel.ToggleHistory\r\n'
                  b'function OnLoad(document)\r\nend\r\n')
        entry = dict(name='player_panel.lua')
        result = resources.apply_edits(source, entry)
        self.assertTrue(result.startswith(source))
        self.assertNotIn(b'\n', result.replace(b'\r\n', b''))
        with self.assertRaisesRegex(ValueError, 'native callbacks changed'):
            resources.apply_edits(result, entry)


class PlayerFilterResourceScope(unittest.TestCase):
    def test_no_row_hiding_or_native_identifier_changes(self):
        entries = json.loads((ROOT / 'profiles/resource-edits.json').read_text(encoding='utf-8'))['ui_entries']
        panel = next(row for row in entries if row['name'] == 'player_panel.rml')
        inserted = next(edit['text'] for edit in panel['edits'] if 'zh_player_filters' in edit.get('text', ''))
        for name in ('all', 'friends', 'company', 'staff'):
            self.assertIn('id="zh_filter_' + name + '"', inserted)
        self.assertNotIn('style=', ElementTree.tostring(ElementTree.fromstring(inserted), encoding='unicode').split('>')[0])
        self.assertIn('data-if="online"', inserted)
        rows = next(row for row in entries if row['name'] == 'player_list.rml')
        self.assertNotIn('player.friend', json.dumps(rows))
        self.assertIn('ui/player_filters.lua', resources.SOURCES)


if __name__ == '__main__':
    unittest.main()
