void AddPvpTitlesScripts();
void AddPvpTitlesAddonScripts();
void AddPvpTitlesDishonorScripts();

// The loader is named after the module directory (modules/CMakeLists.txt):
// "mod-pvp-titles-ext" gives Addmod_pvp_titles_extScripts
void Addmod_pvp_titles_extScripts()
{
    AddPvpTitlesScripts();
    AddPvpTitlesAddonScripts();
    AddPvpTitlesDishonorScripts();
}
