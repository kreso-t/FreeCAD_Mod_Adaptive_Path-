import PathAdaptiveCore
import pygame
import sys
import time
pygame.init()
# x_off = 1024/2 - 200
# y_off = 768/2 + 200
# scale = 4.0

x_off = 1024/2 - 200
y_off = 768/2 + 1500
scale = 20.0

screen=pygame.display.set_mode((1024,768))
screen.fill((255,255,255))
toolDia = 5
paths=[]

def transCoord(pos):
    return [x_off + int(scale*pos[0]) ,y_off - int(scale*pos[1])]

def drawPaths(paths, closed = False):
    for path in paths:
        if len(path)>0:
            pts = []
            for p in path:
                pts.append(transCoord(p))
            pygame.draw.lines(screen,(255,0,0),closed,pts,1)

def drawToolPos(pos, direction, color):
    tp1 = transCoord(pos)
    pygame.draw.circle(screen, color,tp1 , int(scale*toolDia/2), 2)
    tp2 = transCoord([pos[0] + direction[0] * 500,pos[1] + direction[1] * 500])
    pygame.draw.lines(screen,color,False,[tp1,tp2],1)

count = 0

def clear():
    screen.fill((255,255,255))
    drawPaths(paths)
def doEvents():
	for event in pygame.event.get():
	    if event.type == pygame.QUIT:
	        pygame.quit()
	        sys.exit()
def feecback(a):
    global count
    #print "feedback",a.CurrentPath
    count = count +1
    # if count > 10:
    #     clear()
    #     count =0

    drawPaths([a.CurrentPath])
    drawToolPos(a.EngagePos,a.EngageDir,(255,0,0))
    drawToolPos(a.ToolPos,a.ToolDir,(255,0,255))
    pygame.display.update()

    ##time.sleep(0.001)

def getColor(color):
	if color==0: return (0,0,0)
	if color==1: return (255,0,0)
	if color==2: return (0,255,0)
	if color==3: return (0,0,255)
	if color==4: return (255,0,255)
	if color==5: return (255,255,0)
	return (0,0,0)

def drawCircle(x,y, radius, color):
	pygame.draw.circle(screen, getColor(color),transCoord([x,y]) ,int(radius*scale)+1, 1)
	pygame.display.update()
	time.sleep(0.1)
	doEvents()

def drawPath(path, color):
	pts = []
  	for p in path:
	      pts.append(transCoord(p))
	pygame.draw.lines(screen,getColor(color),False,pts,1)
	doEvents()

a2d = PathAdaptiveCore.Adaptive2d()
a2d.DrawCircleFn = drawCircle
a2d.ClearScreenFn = clear
a2d.DrawPathFn = drawPath

a2d.toolDiameter = toolDia

a2d.polyTreeNestingLimit = 1
a2d.SetProgressCallbackFn(feecback)

path0 = [[-10,-10],[110,-10], [110,110], [-10,110]]
path1 = [[0,0],[100,0], [100,100], [0,100]]
path2 = [[30,30],[70,30], [70,70], [30,70]]
path2.reverse()

# path3 = [[40,40],[60,40], [60,60], [40,60]]
# path4 = [[140,140],[160,140], [160,160], [140,160]]
# paths = [path1,path2, path3,path4]
paths.append(path1)
paths.append(path2)
clear()
a2d.Execute(paths)


pygame.quit()
