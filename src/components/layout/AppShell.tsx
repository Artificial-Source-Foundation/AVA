import { ParentComponent } from "solid-js";
import { Sidebar } from "./Sidebar";
import { TabBar } from "./TabBar";
import { StatusBar } from "./StatusBar";

export const AppShell: ParentComponent = (props) => {
  return (
    <div class="flex h-screen bg-gray-900 text-gray-100">
      {/* Sidebar */}
      <Sidebar />

      {/* Main content area */}
      <div class="flex flex-1 flex-col">
        {/* Tab bar for multi-session */}
        <TabBar />

        {/* Main content */}
        <main class="flex-1 overflow-hidden">
          {props.children}
        </main>

        {/* Status bar */}
        <StatusBar />
      </div>
    </div>
  );
};
