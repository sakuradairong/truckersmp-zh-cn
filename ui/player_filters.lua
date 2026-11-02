-- Extend the original OnLoad and retain its native search, history and actions.
do
    local native_on_load = OnLoad
    local names = {"all", "friends", "company", "staff"}

    function OnLoad(document)
        native_on_load(document)
        local bar = document:GetElementById("zh_player_filters")
        if bar == nil then return end
        local visible = true

        local function escaped(text)
            return (tostring(text):gsub("[%z\1-\31\127]", " "):gsub("&", "&amp;")
                :gsub("<", "&lt;"):gsub(">", "&gt;"):gsub('"', "&quot;"):gsub("'", "&#39;"))
        end
        local function write_profile(id, value)
            local element = document:GetElementById("zh_profile_" .. id)
            if element ~= nil then
                local text = escaped(value)
                if element.inner_rml ~= text then element.inner_rml = text end
            end
        end
        local function clear_profile(message)
            write_profile("name", message)
            write_profile("account", "—"); write_profile("server", "—"); write_profile("vtc", "—")
            if type(PlayerPanel.ClearLocalProfile) == "function" then pcall(PlayerPanel.ClearLocalProfile) end
        end
        local function refresh_profile()
            local section = document:GetElementById("zh_profile_section")
            local profile = document:GetElementById("zh_local_profile")
            if section == nil or profile == nil then return end
            if not visible or not section:HasAttribute("data-online") or not profile:HasAttribute("data-public") or
                    section:HasAttribute("data-offline") or profile:HasAttribute("data-private") then
                clear_profile("个人资料已隐藏")
                return
            end
            if type(PlayerPanel.GetLocalProfile) ~= "function" then
                clear_profile("需要更新并启用原生模块")
                return
            end
            local ok, account, server, name, status, vtc, vtc_id = pcall(PlayerPanel.GetLocalProfile)
            if not ok or type(account) ~= "number" or account <= 0 or type(server) ~= "number" or
                    type(name) ~= "string" or name == "" then
                clear_profile("等待当前玩家数据")
                return
            end
            write_profile("name", name); write_profile("account", account); write_profile("server", server)
            local states = {[0]="正在查询…", [2]="未加入 VTC", [3]="查询失败，稍后重试", [4]="查询频率受限，稍后重试"}
            if status == 1 and type(vtc) == "string" and vtc ~= "" and type(vtc_id) == "number" and vtc_id > 0 then
                write_profile("vtc", vtc .. " · #" .. vtc_id)
            else
                write_profile("vtc", states[status] or "暂未获取到")
            end
        end

        local function ready()
            if type(PlayerPanel.FiltersReady) ~= "function" then return false end
            local ok, value = pcall(PlayerPanel.FiltersReady)
            return ok and value == 1
        end

        local function hide()
            bar:SetAttribute("class", "")
        end

        local function current_filter()
            local ok, index = pcall(PlayerPanel.GetFilter)
            if ok and type(index) == "number" then return names[index + 1] end
        end

        local function show_filter(name)
            for _, mode in ipairs(names) do
                local button = document:GetElementById("zh_filter_" .. mode)
                if button ~= nil then
                    button:SetAttribute("class", mode == name and
                        "txt_button zh_filter_tab zh_filter_selected" or "txt_button zh_filter_tab")
                end
            end
            -- The online data binding owns inline display; readiness belongs in CSS.
            bar:SetAttribute("class", "zh_filters_ready")
        end

        local function select_filter(name)
            if not ready() then hide() return end
            local list = document:GetElementById("players")
            if list == nil then hide() return end
            if current_filter() == name then show_filter(name) return end
            local old_scroll = list.scroll_top
            list.scroll_top = 0
            local ok, accepted = pcall(PlayerPanel.SetFilter, name)
            if not ok or accepted ~= 1 then
                list.scroll_top = old_scroll
                hide()
                return
            end
            show_filter(name)
        end

        for _, name in ipairs(names) do
            local mode = name
            local button = document:GetElementById("zh_filter_" .. mode)
            if button ~= nil then
                button:AddEventListener("click", function() select_filter(mode) end)
            end
        end

        local function reset_scroll()
            local list = document:GetElementById("players")
            if list ~= nil then list.scroll_top = 0 end
        end
        document:GetElementById("toggle_history"):AddEventListener("click", reset_scroll)
        local search = document:QuerySelector("#search_players_input_container input")
        if search ~= nil then search:AddEventListener("change", reset_scroll) end

        local function restore_filter()
            refresh_profile()
            if not ready() then hide() return end
            local name = current_filter()
            if name == nil then hide() return end
            -- Restoring controls must not rebuild the list or change its viewport.
            show_filter(name)
        end
        -- The native adapter may bind after this document was loaded.
        PlayerPanel.ZHRefreshFilterTabs = restore_filter
        document:AddEventListener("show", function() visible = true; restore_filter() end)
        document:AddEventListener("hide", function() visible = false; clear_profile("等待当前玩家数据") end)
        document:AddEventListener("player_panel__server_info", function() clear_profile("等待当前玩家数据") end)
        restore_filter()
    end
end
