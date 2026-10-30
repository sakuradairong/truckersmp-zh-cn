-- Extend the original OnLoad and retain its native search, history and actions.
do
    local native_on_load = OnLoad
    local names = {"all", "friends", "company", "staff"}

    function OnLoad(document)
        native_on_load(document)
        local bar = document:GetElementById("zh_player_filters")
        if bar == nil then return end

        local function ready()
            if type(PlayerPanel.FiltersReady) ~= "function" then return false end
            local ok, value = pcall(PlayerPanel.FiltersReady)
            return ok and value == 1
        end

        local function hide()
            bar:SetAttribute("style", "display: none;")
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
            bar:SetAttribute("style", "display: flex;")
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
            if not ready() then hide() return end
            local name = current_filter()
            if name == nil then hide() return end
            -- Restoring controls must not rebuild the list or change its viewport.
            show_filter(name)
        end
        -- The native adapter may bind after this document was loaded.
        PlayerPanel.ZHRefreshFilterTabs = restore_filter
        document:AddEventListener("show", restore_filter)
        restore_filter()
    end
end
