#pragma once

#include <utility>

class WorkspaceStartupSequence
{
  public:
    template <typename RestoreWorkspace, typename ShowWindow, typename DeferServices>
    static void
    run(RestoreWorkspace&& restoreWorkspace, ShowWindow&& showWindow, DeferServices&& deferServices)
    {
        std::forward<RestoreWorkspace>(restoreWorkspace)();
        std::forward<ShowWindow>(showWindow)();
        std::forward<DeferServices>(deferServices)();
    }
};
