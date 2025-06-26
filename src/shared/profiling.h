#ifdef TRACY_ENABLE

#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>

#define ProfileSetThreadName(s) tracy::SetThreadName((s));
#define ProfileZone ZoneScoped;
#define ProfileZoneNamed(s) ZoneScopedN((s));
#define ProfileMessage(txt) TracyMessageL(txt);
#define ProfilePlotData(s, value) TracyPlot((s), (value));
#define ProfilePlotConfig(plotName, plotConfig) TracyPlotConfig(plotName, plotConfig);

#define ProfilePushFiberZone(fiber_name, zone_name, out_zone_ctx)                        \
  TracyFiberEnter(fiber_name);                                                           \
  TracyCZoneN(out_zone_ctx, zone_name, true);                                            \
  TracyFiberLeave;

#define ProfilePopFiberZone(fiber_name, zone_ctx)                                        \
  TracyFiberEnter(fiber_name);                                                           \
  TracyCZoneEnd(zone_ctx);                                                               \
  TracyFiberLeave;

#else

#define ProfileZone
#define ProfileZoneNamed(s) (void)s;
#define ProfilePlotData(s, value) (void)(value);
#define ProfileSetThreadName(s) (void)s;
#define FrameMark
#define ProfilePlotConfig(plotName, plotConfig) ;
#define ProfileMessage(message) (void)message;

#define ProfilePushFiberZone(fiber_name, zone_name, out_zone_ctx) ;
#define ProfilePopFiberZone(fiber_name, zone_ctx) ;

#endif
