/* ShowDrawers.rexx -- open each drawer on Workbench, photograph the screen,
 * close it again: how an installation looks (T=InstallRun: the package
 * drawer and the installed RadeonGL drawers; from Prisma Megamix's
 * test/rexx). One picture a drawer, in its order:
 * DEV:Test/layout-1.ppm, layout-2.ppm, ...
 *
 * Usage: rx ShowDrawers.rexx <drawer> [<drawer> ...]
 */
parse arg drawers
if ~show('P', 'WORKBENCH') then do
    say '  ShowDrawers: no WORKBENCH port, no pictures'
    exit 0
end
do i = 1 to words(drawers)
    d = word(drawers, i)
    address WORKBENCH 'WINDOW "'d'" OPEN'
    if rc ~= 0 then do
        say '  ShowDrawers:' d 'did not open (RC' rc')'
        iterate
    end
    address command 'Wait 2'
    address command 'DEV:Test/Grab 0 0 800 600 DEV:Test/layout-'i'.ppm'
    address WORKBENCH 'WINDOW "'d'" CLOSE'
    say '  layout-'i'.ppm:' d
end
exit 0
