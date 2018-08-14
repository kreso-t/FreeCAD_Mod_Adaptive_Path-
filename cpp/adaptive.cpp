#include "adaptive.hpp"
#include <iostream>
#include <cmath>
#include <cstring>
#include <ctime>


//  re-using some lower level functions from ClipperLib
namespace ClipperLib {
	void TranslatePath(const Path& input, Path& output, IntPoint delta);
	double DistanceFromLineSqrd(const IntPoint& pt, const IntPoint& ln1, const IntPoint& ln2);
	bool SlopesNearCollinear(const IntPoint& pt1,const IntPoint& pt2, const IntPoint& pt3, double distSqrd);
	bool PointsAreClose(IntPoint pt1, IntPoint pt2, double distSqrd);
}

namespace AdaptivePath {
	using namespace ClipperLib;
	using namespace std;


	inline double DistanceSqrd(const IntPoint& pt1, const IntPoint& pt2)
	{
	  double Dx = ((double)pt1.X - pt2.X);
	  double dy = ((double)pt1.Y - pt2.Y);
	  return (Dx*Dx + dy*dy);
	}

	inline bool SetSegmentLength(const IntPoint& pt1, IntPoint& pt2, double new_length)
	{
	  double Dx = ((double)pt2.X - pt1.X);
	  double dy = ((double)pt2.Y - pt1.Y);
	  double l=sqrt(Dx*Dx + dy*dy);
	  if(l>0.0) {
	  	pt2.X = pt1.X + new_length * Dx/l;
	  	pt2.Y = pt1.Y + new_length * dy/l;	
		return true;
	  }
	  return false;
	}

	/*********************************************
	 * Utils
	 ***********************************************/
	/* inline util*/
	inline bool HasAnyPath(const Paths &paths) {
		for(Paths::size_type i=0;i<paths.size();i++) {
			if(paths[i].size()>0) return true;
		}
		return false;
	}

	inline double averageDV(const vector<double> & vec) {
		double s=0;
		std::size_t size = vec.size();
		if(size==0) return 0;
		for(std::size_t i=0;i<size;i++) s+=vec[i];
		return s/double(size);
	}

	inline DoublePoint rotate(const DoublePoint &in, double rad) {
	   double c =cos(rad);
       double s =sin(rad);
	   return DoublePoint(c*in.X-s*in.Y,s*in.X + c*in.Y);
	}

	/* geom utils */
	void AverageDirection(const vector<DoublePoint> &unityVectors, DoublePoint& output) {
		int size=unityVectors.size();
		output.X =0;
		output.Y=0;
		// sum vectors
		for(int i=0;i<size;i++) {
			DoublePoint v= unityVectors[i];
			output.X += v.X;
			output.Y += v.Y;
		}
		// normalize
		double magnitude = sqrt(output.X*output.X + output.Y*output.Y);
		output.X/=magnitude;
		output.Y/=magnitude;
	}

	double DistancePointToLineSegSquared(const IntPoint& p1, const IntPoint& p2,const IntPoint& pt,  IntPoint &closestPoint,bool clamp=true) {
		double D21X=double(p2.X-p1.X);
		double D21Y=double(p2.Y-p1.Y);
		double DP1X=double(pt.X-p1.X);
		double DP1Y=double(pt.Y-p1.Y);
		double lsegLenSqr = D21X*D21X + D21Y*D21Y;
		if (lsegLenSqr==0) { // segment is zero length, return point to point distance
			closestPoint=p1;
			return DP1X*DP1X+DP1Y*DP1Y;
		}
		double parameter = DP1X*D21X + DP1Y*D21Y;
		if(clamp) {
			// clamp the parameter
			if(parameter<0) parameter=0;
			else if(parameter>lsegLenSqr) parameter=lsegLenSqr;
		}
		// point on line at parameter
		closestPoint.X = p1.X + parameter*D21X/lsegLenSqr;
		closestPoint.Y = p1.Y + parameter*D21Y/lsegLenSqr;
		// calculate distance from point on line to pt
		double DX=double(pt.X-closestPoint.X);
		double DY=double(pt.Y-closestPoint.Y);
		return DX*DX+DY*DY; // return distance squared
	}

	// joins collinear segments (within the tolerance)
	void CleanPath(const Path &inp, Path &outp, double tolerance) {
		bool first=true;
		outp.clear();
		for(const auto & pt : inp) {
			if(first) {
				first=false;
				outp.push_back(pt);
			} else {
				if(outp.size()>2) {
					IntPoint clp; // to hold closest point
					double distSqrd = DistancePointToLineSegSquared(outp[outp.size()-2],outp[outp.size()-1],pt,clp,false);
					if(sqrt(distSqrd)<tolerance) {
						outp.pop_back();
						outp.push_back(pt);
					} else {
						outp.push_back(pt);
					}
				} else if(sqrt(DistanceSqrd(outp[outp.size()-1],pt))<tolerance) {
						outp.pop_back();
						outp.push_back(pt);
				} else {
					outp.push_back(pt);
				}
			}
		}
	}

	double DistancePointToPathsSqrd(const Paths &paths, const IntPoint & pt, IntPoint &closestPointOnPath) {
		double minDistSq=__DBL_MAX__;
		IntPoint clp;
		// iterate though paths
		for(Path::size_type i=0;i<paths.size();i++) {
			const Path * path = &paths[i];
			Path::size_type size=path->size();
			// iterate through segments
			for(Path::size_type j=0;j<size;j++) {
				double distSq=DistancePointToLineSegSquared(path->at(j>0 ? j-1 : size-1),path->at(j),pt,clp);
				if(distSq<minDistSq) {
					closestPointOnPath=clp;
					minDistSq=distSq;
				}
			}
		}
		return minDistSq;
	}

	bool Circle2CircleIntersect(const IntPoint & c1, const IntPoint &c2, double radius, pair<DoublePoint,DoublePoint> & intersections ) {
		double DX = double(c2.X - c1.X);
		double DY = double(c2.Y - c1.Y);
		double d = sqrt(DX*DX+DY*DY);
		if(d<NTOL) return false; // same center
		if(d>=radius) return false; // do not intersect, or intersect in one point (this case not relevant here)
		double a_2 = sqrt(4*radius*radius-d*d)/2.0;
		intersections.first = DoublePoint(0.5*(c1.X+c2.X)-DY*a_2/d, 0.5*(c1.Y+c2.Y)+DX*a_2/d);
		intersections.second = DoublePoint(0.5*(c1.X+c2.X)+DY*a_2/d, 0.5*(c1.Y+c2.Y)-DX*a_2/d);
		return true;
	}

	inline double PointSideOfLine(const IntPoint& p1, const IntPoint& p2,const IntPoint& pt) {
		return (pt.X - p1.X)*(p2.Y-p1.Y) - (pt.Y - p2.Y)*(p2.X-p1.X);
	}

	inline double Angle3Points(const DoublePoint & p1,const DoublePoint& p2, const DoublePoint& p3) {
		  double t1= atan2(p1.Y-p2.Y,p1.X-p2.X);
    	  double t2=atan2(p3.Y-p2.Y,p3.X-p2.X);
		  double  a = fabs( t2 - t1 );
    	  return min(a,2*M_PI-a);
	}

	bool Line2CircleIntersect(const IntPoint &c, double radius,const IntPoint &p1, const IntPoint &p2, vector<DoublePoint> & result, bool clamp=true)
	{
		// if more intersections returned, first is closer to p1
		//to do: box  check for performance
		 double dx=double(p2.X-p1.X);
    	 double dy=double(p2.Y-p1.Y);
    	 double lcx = double(p1.X - c.X);
    	 double lcy = double(p1.Y - c.Y);
    	 double a=dx*dx+dy*dy;
    	 double b=2*dx*lcx+2*dy*lcy;
    	 double C=lcx*lcx+lcy*lcy-radius*radius;
    	 double sq = b*b-4*a*C;
    	 if (sq<0) return false; // no solution
    	 sq=sqrt(sq);
    	 double t1=(-b-sq)/(2*a);
    	 double t2=(-b+sq)/(2*a);
		 result.clear();
    	if(clamp) {
        	if (t1>=0.0 && t1<=1.0) result.push_back(DoublePoint(p1.X + t1*dx, p1.Y + t1*dy));
        	if (t2>=0.0 && t2<=1.0) result.push_back(DoublePoint(p1.X + t2*dx, p1.Y + t2*dy));
		} else {
			result.push_back(DoublePoint(p1.X + t2*dx, p1.Y + t2*dy));
			result.push_back(DoublePoint(p1.X + t2*dx, p1.Y + t2*dy));
		}
		return result.size()>0;
	}

	// calculate center point of polygon
	IntPoint Compute2DPolygonCentroid(const Path &vertices)
	{
	    IntPoint centroid(0,0);
	    double signedArea = 0.0;
	    double x0 = 0.0; // Current vertex X
	    double y0 = 0.0; // Current vertex Y
	    double x1 = 0.0; // Next vertex X
	    double y1 = 0.0; // Next vertex Y
	    double a = 0.0;  // Partial signed area

	    // For all vertices
	    int i=0;
		Path::size_type size = vertices.size();
	    for (i=0; i<size; ++i)
	    {
	        x0 = vertices[i].X;
	        y0 = vertices[i].Y;
	        x1 = vertices[(i+1) % size].X;
	        y1 = vertices[(i+1) % size].Y;
	        a = x0*y1 - x1*y0;
	        signedArea += a;
	        centroid.X += (x0 + x1)*a;
	        centroid.Y += (y0 + y1)*a;
	    }

	    signedArea *= 0.5;
	    centroid.X /= (6.0*signedArea);
	    centroid.Y /= (6.0*signedArea);
    	return centroid;
	}

	// point must be within first path (boundary) and must not be within all other paths (holes)
	bool IsPointWithinCutRegion(const Paths & toolBoundPaths,const IntPoint & point) {
		for(size_t i=0; i<toolBoundPaths.size();i++) {
			int pip=PointInPolygon(point, toolBoundPaths[i]);
			if(i==0 && pip==0) return false; // is outside boundary
			if(i>0 && pip!=0) return false; // is inside hole
		}
		return true;
	}

	/* finds intersection of line segment with line segment */
	bool IntersectionPoint(const IntPoint & s1p1,
						const IntPoint & s1p2,
						const IntPoint & s2p1,
						const IntPoint & s2p2,
						IntPoint & intersection) {
		// todo: bounds check for perfomance
		double S1DX = double(s1p2.X - s1p1.X);
		double S1DY = double(s1p2.Y - s1p1.Y);
		double S2DX = double(s2p2.X - s2p1.X);
		double S2DY = double(s2p2.Y - s2p1.Y);
		double d=S1DY*S2DX - S2DY*S1DX;
		if(fabs(d)<NTOL) return false; // lines are parallel

		double LPDX = double(s1p1.X - s2p1.X);
		double LPDY = double(s1p1.Y - s2p1.Y);
		double p1d = S2DY*LPDX - S2DX*LPDY;
		double p2d = S1DY*LPDX - S1DX*LPDY;
		if((d<0) && (
			p1d<d || p1d>0 || p2d<d || p2d>0
		)) return false ; // intersection not within segment1
		if((d>0) && (
			p1d<0 || p1d>d || p2d<0 || p2d>d
		)) return true; // intersection not within segment2
		double t=p1d/d;
		intersection=IntPoint(s1p1.X + S1DX*t, s1p1.Y + S1DY*t);
		return true;
	}

	/* finds one/first intersection of line segment with paths */
	bool IntersectionPoint(const Paths & paths,const IntPoint & p1, const IntPoint & p2, IntPoint & intersection) {
		for(size_t i=0; i< paths.size(); i++) {
			const Path *path = &paths[i];
			size_t size=path->size();
			if(size<2) continue;
			for(size_t j=0;j<size;j++) {
				// todo: box check for perfomance
				const IntPoint * pp1 = &path->at(j>0?j-1:size-1);
				const IntPoint * pp2 = &path->at(j);
				double LDY = double(p2.Y - p1.Y);
				double LDX = double(p2.X - p1.X);
				double PDX = double(pp2->X - pp1->X);
				double PDY = double(pp2->Y - pp1->Y);
				double d=LDY*PDX - PDY*LDX;
				if(fabs(d)<NTOL) continue; // lines are parallel

				double LPDX = double(p1.X - pp1->X);
				double LPDY = double(p1.Y - pp1->Y);
				double p1d = PDY*LPDX - PDX*LPDY;
				double p2d = LDY*LPDX - LDX*LPDY;
				if((d<0) && (
					p1d<d || p1d>0 || p2d<d || p2d>0
				)) continue; // intersection not within segment
				if((d>0) && (
					p1d<0 || p1d>d || p2d<0 || p2d>d
				)) continue; // intersection not within segment
				double t=p1d/d;
				intersection=IntPoint(p1.X + LDX*t, p1.Y + LDY*t);
				return true;
			}
		}
		return false;
	}

	// helper class for measuring performance
	class PerfCounter {
		public:
			PerfCounter(string p_name) {
				name = p_name;
				count =0;
			}
			void Start() {
				start_ticks=clock();
			}
			void Stop() {
				total_ticks+=clock()-start_ticks;
				count++;
			}
			void DumpResults() {
				double total_time=double(total_ticks)/CLOCKS_PER_SEC;
				cout<<"Perf: " << name.c_str() << " total_time: " <<  total_time  << " sec, call_count:" << count << " per_call:" << double(total_time/count) << endl;
			}
		private:
			string name;
			clock_t start_ticks;
			clock_t total_ticks;
			size_t count;
	};

	PerfCounter Perf_ProcessPolyNode("ProcessPolyNode");
	PerfCounter Perf_CalcCutArea("CalcCutArea");
	PerfCounter Perf_NextEngagePoint("NextEngagePoint");
	PerfCounter Perf_PointIterations("PointIterations");
	PerfCounter Perf_ExpandCleared("ExpandCleared");
	PerfCounter Perf_DistanceToBoundary("DistanceToBoundary");

	/*****************************************
	 * Linear Interpolation - area vs angle
	 * ***************************************/
	class Interpolation {
		public:
			const double MIN_ANGLE = -M_PI/4;
			const double MAX_ANGLE = M_PI/4;

			void clear() {
				angles.clear();
				areas.clear();
			}
			// adds point keeping the incremental order of areas in order for interpolation to work correctly
			void addPoint(double area, double angle) {
				std::size_t size = areas.size();
				if(size==0 || area > areas[size-1]) { // first point or largest area point
					areas.push_back(area);
					angles.push_back(angle);
					return;
				}

				for(std::size_t i=0;i<size;i++) {
					if(area<areas[i]) {
						areas.insert(areas.begin() + i,area);
						angles.insert(angles.begin() + i,angle);
					}
				}
			}

			double interpolateAngle(double targetArea) {
				std::size_t size = areas.size();
				if(size<2 || targetArea>areas[size-1]) return MIN_ANGLE; //max engage angle - convinient value to initially measure cut area
				if(targetArea<areas[0]) return MAX_ANGLE; // min engage angle

				for(size_t i=1;i<size;i++) {
					// find 2 subsequent points where target area is between
					if(areas[i-1]<=targetArea && areas[i]>targetArea) {
						// linear interpolation
						double af = (targetArea-areas[i-1])/(areas[i] - areas[i-1]);
						double a = angles[i-1]  + af*(angles[i] - angles[i-1]);
						return a;
					}
				}
				return MIN_ANGLE;
			}

			double clampAngle(double angle) {
				if(angle<MIN_ANGLE) return MIN_ANGLE;
				if(angle>MAX_ANGLE) return MAX_ANGLE;
				return angle;
			}

			double getRandomAngle() {
				return MIN_ANGLE + (MAX_ANGLE-MIN_ANGLE)*double(rand())/double(RAND_MAX);
			}
			size_t getPointCount() {
				return areas.size();
			}

		private:
			vector<double> angles;
			vector<double> areas;

	};

	/****************************************
	 * Engage Point
	 ***************************************/
	class EngagePoint {
		public:
			EngagePoint(const Paths & p_toolBoundPaths) {
				toolBoundPaths=&p_toolBoundPaths;
				currentPathIndex=0;
				currentSegmentIndex=0;
				segmentPos =0;
				totalDistance=0;
				calculateCurrentPathLength();
			}


		void moveToClosestPoint(const IntPoint &pt,double step) {
				double minDistSq = __DBL_MAX__;
				size_t minPathIndex = currentPathIndex;
				size_t minSegmentIndex = currentSegmentIndex;
				double minSegmentPos = segmentPos;
				totalDistance=0;
				for(;;) {
					while(moveForward(step)) {
						double distSqrd = DistanceSqrd(pt,getCurrentPoint());
						if(distSqrd<minDistSq) {
							//cout << sqrt(minDistSq) << endl;
							minDistSq = distSqrd;
							minPathIndex = currentPathIndex;
							minSegmentIndex = currentSegmentIndex;
							minSegmentPos = segmentPos;
						}
					}
					if(!nextPath()) break;
				}
				currentPathIndex=minPathIndex;
				currentSegmentIndex=minSegmentIndex;
				segmentPos=minSegmentPos ;
				calculateCurrentPathLength();
				passes=0;
		}
		bool nextEngagePoint(Adaptive2d*parent,  const Paths & cleared, double step, double minCutArea, double maxCutArea) {
			Perf_NextEngagePoint.Start();
			IntPoint initialPoint = getCurrentPoint();
			for(;;) {
				if(!moveForward(step))	 {
					if(!nextPath()) {
						passes++;
						if(passes>1) {
							Perf_NextEngagePoint.Stop();
							return false; // nothin more to cut
						}
					}
				}
				IntPoint cpt = getCurrentPoint();
				double area=parent->CalcCutArea(clip,initialPoint,cpt,cleared);			
				if(area>minCutArea && area<maxCutArea) {
					Perf_NextEngagePoint.Stop();
					return true;
				}
			}
		}
			IntPoint getCurrentPoint() {
				const Path * pth = &toolBoundPaths->at(currentPathIndex);
				const IntPoint * p1=&pth->at(currentSegmentIndex>0?currentSegmentIndex-1:pth->size()-1);
				const IntPoint * p2=&pth->at(currentSegmentIndex);
				double segLength =sqrt(DistanceSqrd(*p1,*p2));
				return IntPoint(p1->X + segmentPos*double(p2->X-p1->X)/segLength,p1->Y + segmentPos*double(p2->Y-p1->Y)/segLength);
			}

			DoublePoint getCurrentDir() {
				const Path * pth = &toolBoundPaths->at(currentPathIndex);
				const IntPoint * p1=&pth->at(currentSegmentIndex>0?currentSegmentIndex-1:pth->size()-1);
				const IntPoint * p2=&pth->at(currentSegmentIndex);
				double segLength =sqrt(DistanceSqrd(*p1,*p2));
				return DoublePoint(double(p2->X-p1->X)/segLength,double(p2->Y-p1->Y)/segLength);
			}

			bool moveForward(double distance) {
				const Path * pth = &toolBoundPaths->at(currentPathIndex);
				if(distance<NTOL) throw std::invalid_argument( "distance must be positive" );
				totalDistance+=distance;
				double segmentLength =  currentSegmentLength();
				while(segmentPos+distance>segmentLength) {
					currentSegmentIndex++;
					if(currentSegmentIndex>=pth->size()) {
						currentSegmentIndex=0;
					}
					distance=distance-(segmentLength-segmentPos);
					segmentPos =0;
					segmentLength =currentSegmentLength();
				}
				segmentPos+=distance;
				return totalDistance<=currentPathLength+10;
			}

			bool nextPath() {
				currentPathIndex++;
				currentSegmentIndex=0;
				segmentPos =0;
				totalDistance=0;
				if(currentPathIndex>=toolBoundPaths->size()) {
					currentPathIndex =0;
					calculateCurrentPathLength();
					return false;
				}
				calculateCurrentPathLength();
				//cout << "nextPath:" << currentPathIndex << endl;
				return true;
			}

		private:
			const Paths * toolBoundPaths;
			size_t currentPathIndex;
			size_t currentSegmentIndex;
			double segmentPos =0;
			double totalDistance=0;
			double currentPathLength=0;
			int passes=0;
			Clipper clip;
			void calculateCurrentPathLength() {
				const Path * pth = &toolBoundPaths->at(currentPathIndex);
				size_t size=pth->size();
				currentPathLength=0;
				for(size_t i=0;i<size;i++) {
					const IntPoint * p1=&pth->at(i>0?i-1:size-1);
					const IntPoint * p2=&pth->at(i);
					currentPathLength += sqrt(DistanceSqrd(*p1,*p2));
				}
			}

			double currentSegmentLength() {
				const Path * pth = &toolBoundPaths->at(currentPathIndex);
				const IntPoint * p1=&pth->at(currentSegmentIndex>0?currentSegmentIndex-1:pth->size()-1);
				const IntPoint * p2=&pth->at(currentSegmentIndex);
				return sqrt(DistanceSqrd(*p1,*p2));
			}






	};
	/****************************************
	// Adaptive2d - constructor
	*****************************************/
	Adaptive2d::Adaptive2d() {				
	}

	double Adaptive2d::CalcCutArea(Clipper & clip,const IntPoint &c1, const IntPoint &c2, const Paths &cleared_paths) {
		Perf_CalcCutArea.Start();

		double dist = DistanceSqrd(c1,c2);
		if(dist<NTOL) return 0;

		// // /// old way of calculating cut area:
		// // 1. find differene beween old and new tool shape
		// Path oldTool;
		// Path newTool;
		// TranslatePath(toolGeometry,oldTool,c1);
		// TranslatePath(toolGeometry,newTool,c2);
		// clip.Clear();
		// clip.AddPath(newTool, PolyType::ptSubject, true);
		// clip.AddPath(oldTool, PolyType::ptClip, true);
		// Paths toolDiff;
		// clip.Execute(ClipType::ctDifference,toolDiff);

		// // 2. difference to cleared
		// clip.Clear();
		// clip.AddPaths(toolDiff,PolyType::ptSubject, true);
		// clip.AddPaths(cleared_paths,PolyType::ptClip, true);
		// Paths cutAreaPoly;
		// clip.Execute(ClipType::ctDifference, cutAreaPoly);

		// // calculate resulting area
		// double areaSum=0;
		// for(Path &path : cutAreaPoly) {
		// 	areaSum += fabs(Area(path));
		// }


		/// new alg

		double rsqrd=toolRadiusScaled*toolRadiusScaled;
    	double area =0;

		for(const Path &path : cleared_paths) {
			size_t size = path.size();
			size_t curPtIndex = 0;
			bool found=false;
			// step 1: we find the starting point on the cleared path that is outside new tool shape (c2)
			for(size_t i=0;i<size;i++) {
				if(DistanceSqrd(path[curPtIndex],c2)>rsqrd) {
					found = true;
					break;
				}
				curPtIndex++; if(curPtIndex>=size) curPtIndex=0;
			}
			if(!found) continue; // try anohter path

			// step 2: iterate throuh path from starting point and find the part of the path inside the c2
			size_t prevPtIndex = curPtIndex;			
			IntPoint clp; // to hold closest point
			vector<DoublePoint> inters; // to hold intersection results
			Path innerPathC2;
			bool prev_inside=false;
			const IntPoint *p1=&path[prevPtIndex];
			bool process=false;	
			bool debug=false;		
			for(size_t i=0;i<size;i++) {
				curPtIndex++; if(curPtIndex>=size) curPtIndex=0;			
				const IntPoint *p2=&path[curPtIndex];				
				if(!prev_inside) { // prev state: outside, find first point inside C2
					// TODO:BBOX check here	maybe				
					if(DistancePointToLineSegSquared(*p1,*p2,c2, clp)<=rsqrd) {  // current segment inside, start
						prev_inside=true;
						innerPathC2.clear();
						// current segment inside c2, prev point outside, find intersection:							
						if(Line2CircleIntersect(c2,toolRadiusScaled,*p1,*p2,inters)) {
							innerPathC2.push_back(IntPoint(inters[0].X,inters[0].Y));
							if(inters.size()>1) {
								innerPathC2.push_back(IntPoint(inters[1].X,inters[1].Y));
								process=true;				
								prev_inside=false;
							} else {
								innerPathC2.push_back(IntPoint(*p2));
							}
						} else { // no intersection - must be edge case, add p2
							//prev_inside=false;
							innerPathC2.push_back(IntPoint(*p2));
						}
					}
				} else { // state: inside
					if( (DistanceSqrd(c2,*p2) <= rsqrd)) {  // next point still inside, add it and continue, no state change
						innerPathC2.push_back(IntPoint(*p2));
					} else { // prev point inside, current point outside, find instersection
						if(Line2CircleIntersect(c2,toolRadiusScaled,*p1,*p2,inters)) {
							if(inters.size()>1) {
								innerPathC2.push_back(IntPoint(inters[1].X,inters[1].Y));
							} else {
								innerPathC2.push_back(IntPoint(inters[0].X,inters[0].Y));
							}													
						} 
						process=true;	
						prev_inside=false;						
					}
				}
				
				
				if(process) {						
						process=false;
						// innerPathC2 - now contains the part of cleared path inside the C2
						size_t ipc2_size =innerPathC2.size();
						const IntPoint &fpc2=innerPathC2[0]; // first point
						const IntPoint &lpc2=innerPathC2[ipc2_size-1]; // last point
						// path length
						double innerPathC2Len=0;
						for(size_t j=1;j<ipc2_size;j++) innerPathC2Len+=sqrt(DistanceSqrd(innerPathC2[j-1],innerPathC2[j]));

						// directions						
						const double fdx=double(lpc2.X-fpc2.X); // general path direction
						const double fdy=double(lpc2.Y-fpc2.Y); // general path direction
						const double cutdx=double(c2.X-c1.X); // cutting direction
						const double cutdy=double(c2.Y-c1.Y); // cutting direction
						bool reverse=false;
						// check & reverse	if necessary
						double diff =  fabs(atan2(cutdy,cutdx)- atan2(fdy,fdx));							  
						diff = min(diff,2*M_PI-diff);    	  												
						if(diff>M_PI_2) reverse=true;

						//cout << side << endl;
						Paths inPaths;
						inPaths.push_back(innerPathC2);
						// DrawPath(innerPathC2,10);
						// DrawCircle(fpc2,scaleFactor/4,1);
						// DrawCircle(lpc2,scaleFactor/4,2);
						Path pthToSubtract ;
						pthToSubtract << fpc2;

						double fi1 = atan2(fpc2.Y-c2.Y,fpc2.X-c2.X);
						double fi2 = atan2(lpc2.Y-c2.Y,lpc2.X-c2.X);
						double minFi=fi1;
						double maxFi=fi2;
						if(reverse) {
							minFi=fi2;
							maxFi=fi1;						
						}
						if(maxFi<minFi) maxFi += 2*M_PI;

						double scanDistance = 2.5*toolRadiusScaled;
						
						// stepping through path discretized to stepDistance
						double stepDistance=RESOLUTION_FACTOR+1;
						const IntPoint * prevPt=&innerPathC2[0];
						double distance=0;
						for(size_t j=1;j<ipc2_size;j++) {
							const IntPoint * cpt =&innerPathC2[j];
							double segLen = sqrt(DistanceSqrd(*cpt,*prevPt));
							if(segLen<NTOL) continue; // skip point - segment too short
							for(double pos_unclamped=0.0;pos_unclamped<segLen+stepDistance;pos_unclamped+=stepDistance) {
								double pos=pos_unclamped;
								if(pos>segLen) {
									distance+=stepDistance-(pos-segLen);
									pos=segLen; // make sure we get exact end point
								} else {
									distance+=stepDistance;
								}
								double dx=double(cpt->X-prevPt->X);
								double dy=double(cpt->Y-prevPt->Y);
								IntPoint segPoint(prevPt->X + dx*pos/segLen, prevPt->Y + dy*pos/segLen);
								IntPoint scanPoint(c2.X + scanDistance*cos(minFi + distance*(maxFi-minFi)/innerPathC2Len),
											c2.Y + scanDistance*sin(minFi + distance*(maxFi-minFi)/innerPathC2Len));

								IntPoint intersC2(segPoint.X,segPoint.Y);
								IntPoint intersC1(segPoint.X,segPoint.Y);

								// there should be intersection with C2
								if(Line2CircleIntersect(c2,toolRadiusScaled,segPoint,scanPoint,inters)) {
									if(inters.size()>1) {										
										intersC2.X = inters[1].X;
										intersC2.Y = inters[1].Y;
									} else {
										intersC2.X = inters[0].X;
										intersC2.Y = inters[0].Y;
									}
								} else {
									pthToSubtract.push_back(segPoint);
								}

								if(Line2CircleIntersect(c1,toolRadiusScaled,segPoint,scanPoint,inters)) {
										if(inters.size()>1) {											
											intersC1.X = inters[1].X;
											intersC1.Y = inters[1].Y;
										} else {
											intersC1.X = inters[0].X;
											intersC1.Y = inters[0].Y;
										}
									if(DistanceSqrd(segPoint,intersC2)<DistanceSqrd(segPoint,intersC1)) {
										pthToSubtract.push_back(intersC2);
									} else {
										pthToSubtract.push_back(intersC1);
									}
								} else { // add the segpoint if no intersection with C1
									pthToSubtract.push_back(segPoint);
								}
							}
							prevPt = cpt;
						}

						pthToSubtract << lpc2; // add last point
						pthToSubtract.push_back(c2);

						double segArea =Area(pthToSubtract);
						double A=(maxFi-minFi)*rsqrd/2; // sector area
						// final cut area
						if(!reverse)
							area+=A- fabs(segArea);
						else
						 	area-= (A- fabs(segArea));
				}
				prevPtIndex = curPtIndex;
				p1 = p2;
			}
		}
		//  cout<< "PolyArea:" << areaSum << " new area:" << area << endl;
		Perf_CalcCutArea.Stop();
		return area;
	}

	/****************************************
	// Adaptive2d - Execute
	*****************************************/

 	std::list<AdaptiveOutput> Adaptive2d::Execute(const DPaths &paths,  std::function<bool(TPaths)> progressCallbackFn) {

		//**********************************
		// Initializations
		// **********************************
		scaleFactor = RESOLUTION_FACTOR/tolerance;
		toolRadiusScaled = toolDiameter*scaleFactor/2;
		bbox_size =toolDiameter*scaleFactor;
		progressCallback = &progressCallbackFn;
		lastProgressTime=clock();

		if(helixRampDiameter<=1e-9 || helixRampDiameter>toolDiameter ) {
			helixRampRadiusScaled=toolRadiusScaled;
		} else  {
			helixRampRadiusScaled=helixRampDiameter*scaleFactor/2;
		}

		finishPassOffsetScaled=tolerance*scaleFactor/2;

		//cout<< "toolRadiusScaled:" << toolRadiusScaled << endl;
		ClipperOffset clipof;
		Clipper clip;

		clip.PreserveCollinear(false);
		// generate tool shape
		clipof.Clear();
		Path p;
		p << IntPoint(0,0);
		clipof.AddPath(p,JoinType::jtRound,EndType::etOpenRound);
		Paths toolGeometryPaths;
		clipof.Execute(toolGeometryPaths,toolRadiusScaled);
		//calculate referece area
		Path slotCut;
		TranslatePath(toolGeometryPaths[0],slotCut,IntPoint(toolRadiusScaled/2,0));
		clip.Clear();
		clip.AddPath(toolGeometryPaths[0],PolyType::ptSubject,true);
		clip.AddPath(slotCut,PolyType::ptClip,true);
		Paths crossing;
		clip.Execute(ClipType::ctDifference,crossing);
		referenceCutArea = fabs(Area(crossing[0]));
		optimalCutAreaPD =2 * stepOverFactor * referenceCutArea/toolRadiusScaled;
		minCutAreaPD = optimalCutAreaPD/3 +1; // influences decreasing of cut area near boundary, i.e. avoiding boundary

		// **********************
		// Convert input paths to clipper
		//************************
		for(int i=0;i<paths.size();i++) {
			Path cpth;
			for(int j=0;j<paths[i].size();j++) {
				std::pair<double,double> pt = paths[i][j];
				cpth.push_back(IntPoint(pt.first*scaleFactor,pt.second*scaleFactor));
			}
			inputPaths.push_back(cpth);
		}


		// *******************************
		//	Resolve hierarchy and run processing
		// ******************************
		if(opType==OperationType::otProfilingInside) {
			clipof.Clear();
			clipof.AddPaths(inputPaths,JoinType::jtSquare,EndType::etClosedPolygon);
			Paths off1;
			clipof.Execute(off1,-2*(helixRampRadiusScaled+toolRadiusScaled));

			clip.Clear();
			clip.AddPaths(inputPaths,PolyType::ptSubject,true);
			clip.AddPaths(off1,PolyType::ptClip,true);
			clip.Execute(ClipType::ctDifference,inputPaths,PolyFillType::pftEvenOdd);
		}

		if(opType==OperationType::otProfilingOutside) {
			clipof.Clear();
			clipof.AddPaths(inputPaths,JoinType::jtSquare,EndType::etClosedPolygon);
			Paths off1;
			clipof.Execute(off1,2*(helixRampRadiusScaled+toolRadiusScaled));

			clip.Clear();
			clip.AddPaths(off1,PolyType::ptSubject,true);
			clip.AddPaths(inputPaths,PolyType::ptClip,true);
			clip.Execute(ClipType::ctDifference,inputPaths,PolyFillType::pftEvenOdd);
		}

		clipof.Clear();
		clipof.AddPaths(inputPaths,JoinType::jtRound,EndType::etClosedPolygon);
		PolyTree initialTree;
		clipof.Execute(initialTree,-toolRadiusScaled-finishPassOffsetScaled);

		PolyNode *current = initialTree.GetFirst();
		while(current!=0) {
			if(!current->IsHole()) {
				int nesting = 0;
				PolyNode *parent = current->Parent;
				while(parent->Parent) {
					nesting++;
					parent=parent->Parent;
				}
				//cout<< " nesting:" << nesting << " limit:" << polyTreeNestingLimit << " processHoles:" << processHoles << endl;
				if(polyTreeNestingLimit==0 || nesting<polyTreeNestingLimit) {
					Paths toolBoundPaths;

					toolBoundPaths.push_back(current->Contour);
					if(processHoles) {
						for(int i=0;i<current->ChildCount();i++)
							toolBoundPaths.push_back(current->Childs[i]->Contour);
					}
					// calc bounding paths - i.e. area that must be cleared inside
					// it's not the same as input paths due to filtering (nesting logic)
					Paths boundPaths;
					clipof.Clear();
					clipof.AddPaths(toolBoundPaths,JoinType::jtRound,EndType::etClosedPolygon);
					clipof.Execute(boundPaths,toolRadiusScaled+finishPassOffsetScaled);
					ProcessPolyNode(boundPaths,toolBoundPaths);
				}
			}
			current = current->GetNext();
		}

		return results;
	}

	bool Adaptive2d::FindEntryPoint(const Paths & toolBoundPaths, IntPoint &entryPoint /*output*/) {
		Paths incOffset;
		Paths lastValidOffset;
		ClipperOffset clipof;
		clipof.Clear();
		clipof.AddPaths(toolBoundPaths,JoinType::jtSquare,EndType::etClosedPolygon);
		double step = RESOLUTION_FACTOR;
		double currentDelta=-1;
		clipof.Execute(incOffset,currentDelta);
		while(incOffset.size()>0) {
			clipof.Execute(incOffset,currentDelta);
			if(incOffset.size()>0) lastValidOffset=incOffset;
			currentDelta-=step;
		}
		for(int i=0;i<lastValidOffset.size();i++) {
			if(lastValidOffset[i].size()>0) {
				entryPoint = Compute2DPolygonCentroid(lastValidOffset[i]);
				return true;
			}
		}
		cerr<<"Start point not found!"<<endl;
		return false;
	}

	/**
	 * returns true if line from lastPoint to nextPoint  is clear from obstacles
	*/
	bool  Adaptive2d::CheckCollision(const IntPoint &lastPoint,const IntPoint &nextPoint,const Paths & cleared) {
		Clipper clip;
		ClipperOffset clipof;
		Path tp;
		tp <<lastPoint;
		tp << nextPoint;
		clipof.AddPath(tp,JoinType::jtRound,EndType::etOpenRound);
		Paths toolShape;
		clipof.Execute(toolShape,toolRadiusScaled-2);
		clip.AddPaths(toolShape,PolyType::ptSubject,true);
		clip.AddPaths(cleared,PolyType::ptClip,true);
		Paths crossing;
		clip.Execute(ClipType::ctDifference,crossing);
		double collisionArea =0;
		for(auto &p : crossing) {
			collisionArea += fabs(Area(p));
		}
		return collisionArea <= NTOL;
	}

	void Adaptive2d::AppendToolPath(AdaptiveOutput & output,const Path & passToolPath,const Paths & cleared, bool close) {
		if(passToolPath.size()<1) return;		
		IntPoint nextPoint(passToolPath[0]);
		if(output.AdaptivePaths.size()>0 && output.AdaptivePaths[output.AdaptivePaths.size()-1].Points.size()>0) { // if there is a previous path
			auto & lastTPath = output.AdaptivePaths[output.AdaptivePaths.size()-1];
			auto & lastTPoint = lastTPath.Points[lastTPath.Points.size()-1];			
			IntPoint lastPoint(lastTPoint.first*scaleFactor,lastTPoint.second*scaleFactor);
			bool clear = CheckCollision(lastPoint,nextPoint,cleared);
			// add linking move
			TPath linkPath;
			linkPath.MType = clear ? MotionType::mtLinkClear : MotionType::mtLinkNotClear;
			DPoint nextT;
			nextT.first = double(nextPoint.X)/scaleFactor;
			nextT.second = double(nextPoint.Y)/scaleFactor;
			linkPath.Points.push_back(lastTPoint);
			linkPath.Points.push_back(nextT);
			output.AdaptivePaths.push_back(linkPath);
		// first we find the last point
		}
		TPath cutPath;
		cutPath.MType =MotionType::mtCutting;
		for(const auto &p : passToolPath) {
			DPoint nextT;
			nextT.first = double(p.X)/scaleFactor;
			nextT.second = double(p.Y)/scaleFactor;
			cutPath.Points.push_back(nextT);
		}

		if(close) {
			DPoint nextT;
			nextT.first = double(passToolPath[0].X)/scaleFactor;
			nextT.second = double(passToolPath[1].Y)/scaleFactor;
			cutPath.Points.push_back(nextT);
		}
		output.AdaptivePaths.push_back(cutPath);
	}

	void Adaptive2d::CheckReportProgress(TPaths &progressPaths) {
		if(clock()-lastProgressTime<PROGRESS_TICKS) return; // not yet
		lastProgressTime=clock();
		if(progressPaths.size()==0) return;
		if(progressPaths[progressPaths.size()-1].Points.size()==0) return;
		if(progressCallback) (*progressCallback)(progressPaths); // call python function
		// clean the paths - keep the last point
		TPath * lastPath = &progressPaths[progressPaths.size()-1];
		DPoint *lastPoint =&lastPath->Points[lastPath->Points.size()-1];
		DPoint next(lastPoint->first,lastPoint->second);
		while(progressPaths.size()>1) progressPaths.pop_back();
		while(progressPaths[0].Points.size()>0) progressPaths[0].Points.pop_back();
		progressPaths[0].Points.push_back(next);
	}
	void Adaptive2d::ProcessPolyNode(const Paths & boundPaths, const Paths & toolBoundPaths) {
		Perf_ProcessPolyNode.Start();
		// node paths are already constrained to tool boundary path for adaptive path before finishing pass

		IntPoint entryPoint;
		TPaths progressPaths;

		if(!FindEntryPoint(boundPaths, entryPoint)) return;
		cout << "Entry point:" << entryPoint << endl;

		Paths cleared;
		ClipperOffset clipof;
		AdaptiveOutput output;
		output.HelixCenterPoint.first = double(entryPoint.X)/scaleFactor;
		output.HelixCenterPoint.second =double(entryPoint.Y)/scaleFactor;

		// make initial polygon cleard by helix ramp
		clipof.Clear();
		Path p1;
		p1.push_back(entryPoint);
		clipof.AddPath(p1,JoinType::jtRound,EndType::etOpenRound);
		clipof.Execute(cleared,helixRampRadiusScaled+toolRadiusScaled);
		CleanPolygons(cleared);
		Clipper clip;
		// we got first cleared area - check if it is crossing boundary
		clip.Clear();
		clip.AddPaths(cleared,PolyType::ptSubject,true);
		clip.AddPaths(boundPaths,PolyType::ptClip,true);
		Paths crossing;
		clip.Execute(ClipType::ctDifference,crossing);
		if(crossing.size()>0) {
			cerr<<"Helix does not fit to the cutting area, try limiting the helix diameter to a smaller value."<<endl;
			return;
		}

		long stepScaled;
		IntPoint engagePoint;

		IntPoint toolPos;
		DoublePoint toolDir;

		IntPoint newToolPos;
		DoublePoint newToolDir;


		// find the first tool position and direction
		toolPos = IntPoint(entryPoint.X,entryPoint.Y - helixRampRadiusScaled);
		toolDir = DoublePoint(1.0,0.0);
		bool firstEngagePoint=true;
		Path passToolPath; // to store pass toolpath
		Path toClearPath; // to clear toolpath
		IntPoint clp; // to store closest point
		vector<DoublePoint> gyro; // used to average tool direction
		vector<double> angleHistory; // use to predict deflection angle
		double angle = M_PI;
		engagePoint = toolPos;
		Path boundBox;
		Interpolation interp; // interpolation instance
		EngagePoint engage(toolBoundPaths); // engage point stepping instance

		long total_iterations =0;
		long total_points =0;
		long total_exceeded=0;
		long total_output_points=0;
		double perf_total_len=0;
		clock_t start_clock=clock();

		/*******************************
		 * LOOP - PASSES
		 *******************************/
		for(long pass=0;pass<PASSES_LIMIT;pass++) {
			//cout<<"Pass:"<< pass << endl;
			passToolPath.clear();
			toClearPath.clear();
			angleHistory.clear();

			// append a new path to progress info paths
			if(progressPaths.size()==0) {						
				progressPaths.push_back(TPath());
			} else {
				// append new path if previous not empty
				if(progressPaths[progressPaths.size()-1].Points.size()>0)
						progressPaths.push_back(TPath());
			}

			angle = M_PI_4; // initial pass angle
			bool reachedBoundary = false;
			double cumulativeCutArea=0;
			// init gyro
			gyro.clear();
			for(int i=0;i<DIRECTION_SMOOTHING_BUFLEN;i++) gyro.push_back(toolDir);
			/*******************************
			 * LOOP - POINTS
			 *******************************/
			for(long point_index=0;point_index<POINTS_PER_PASS_LIMIT;point_index++) {
				//cout<<"Pass:"<< pass << " Point:" << point_index;
				total_points++;
				AverageDirection(gyro, toolDir);
				Perf_DistanceToBoundary.Start();
				double distanceToBoundary = sqrt(DistancePointToPathsSqrd(toolBoundPaths, toolPos, clp));
				Perf_DistanceToBoundary.Stop();
				double distanceToEngage = sqrt(DistanceSqrd(toolPos,engagePoint));
				double relDistToBoundary = 2.0 * distanceToBoundary/toolRadiusScaled;

				double targetAreaPD=optimalCutAreaPD;
				// modify/slightly decrease target cut area at the end of cut
				if(relDistToBoundary<1.0 && distanceToEngage>toolRadiusScaled) {
					targetAreaPD = relDistToBoundary*(optimalCutAreaPD-minCutAreaPD) + minCutAreaPD;
				}
				// set the step size
				if(distanceToBoundary<toolRadiusScaled || distanceToEngage<toolRadiusScaled) {
					stepScaled = RESOLUTION_FACTOR*2;
				} else if(fabs(angle)>1e-5) {
					stepScaled = RESOLUTION_FACTOR/fabs(angle);
				} else {
					stepScaled = RESOLUTION_FACTOR*4 ;
				}
				// clamp the step size - for stability
				if(stepScaled<RESOLUTION_FACTOR*2) stepScaled=RESOLUTION_FACTOR*2;
				else if(stepScaled>toolRadiusScaled/2) stepScaled=toolRadiusScaled/2;

				/************************************
				 * ANGLE vs AREA ITERATIONS
				 *********************************/
				double predictedAngle = averageDV(angleHistory);
				double maxError = AREA_ERROR_FACTOR/stepScaled+2;
				double area=0;
				double areaPD=0;
				interp.clear();
				/******************************/
				Perf_PointIterations.Start();
				for(int iteration=0;iteration<MAX_ITERATIONS;iteration++) {
					total_iterations++;
					if(iteration==0) angle=predictedAngle;
					else if(iteration==1) angle=interp.MIN_ANGLE; // max engage
					else if(iteration==3) angle=interp.MAX_ANGLE; // min engage
					else if(interp.getPointCount()<2 || iteration==5 || iteration==9) angle=interp.getRandomAngle();
					else angle=interp.interpolateAngle(targetAreaPD);
					angle=interp.clampAngle(angle);

					newToolDir = rotate(toolDir,angle);
					newToolPos = IntPoint(toolPos.X + newToolDir.X * stepScaled, toolPos.Y + newToolDir.Y * stepScaled);

					area = CalcCutArea(clip, toolPos,newToolPos, cleared);
					areaPD = area/double(stepScaled); // area per distance
					interp.addPoint(areaPD,angle);

					double error=areaPD-targetAreaPD;
					if (fabs(error) < maxError) {
						angleHistory.push_back(angle);
						if(angleHistory.size() > ANGLE_HISTORY_POINTS)
							angleHistory.erase(angleHistory.begin());
						break;
					}
					if(iteration==MAX_ITERATIONS-1) total_exceeded++;
				}
				Perf_PointIterations.Stop();
				/************************************************
				 * CHECK AND RECORD NEW TOOL POS
				 * **********************************************/
				if(distanceToBoundary<toolRadiusScaled
					&& !IsPointWithinCutRegion(toolBoundPaths,newToolPos)) {
						reachedBoundary=true;
						// we reached end of cutting area
						IntPoint boundaryPoint;
						if(IntersectionPoint(toolBoundPaths,toolPos,newToolPos, boundaryPoint)) {
							newToolPos=boundaryPoint;
							area = CalcCutArea(clip,toolPos,newToolPos,cleared);
							areaPD = area/double(stepScaled); // area per distance
						} else {
							newToolPos=toolPos;
							area=0;
							areaPD=0;
						}
				}

				if(area>3*optimalCutAreaPD+10 && areaPD>2*optimalCutAreaPD+10) {
					cerr<<"Break: over cut" << endl;
					break;
				}

				if(toClearPath.size()==0) toClearPath.push_back(toolPos);
				toClearPath.push_back(newToolPos);
				if(firstEngagePoint) { // initial spiral shape need clearing in smaller intervals
					if(toClearPath.size()>10) {
						Perf_ExpandCleared.Start();
						// expand cleared
						clipof.Clear();
						clipof.AddPath(toClearPath,JoinType::jtRound,EndType::etOpenRound);
						Paths toolCoverPoly;
						clipof.Execute(toolCoverPoly,toolRadiusScaled+1);
						clip.Clear();
						clip.AddPaths(cleared,PolyType::ptSubject,true);
						clip.AddPaths(toolCoverPoly,PolyType::ptClip,true);
						clip.Execute(ClipType::ctUnion,cleared);
						CleanPolygons(cleared);
						toClearPath.clear();
						Perf_ExpandCleared.Stop();
					}
				}

				if(area>0) { // cut is ok - record it
					cumulativeCutArea+=area;

					// append to toolpaths
					if(passToolPath.size()==0) passToolPath.push_back(toolPos);
					passToolPath.push_back(newToolPos);
					perf_total_len+=stepScaled;
					toolPos=newToolPos;

					// append to progress info paths
					if(progressPaths.size()==0) {
						progressPaths.push_back(TPath());
					}
					progressPaths[progressPaths.size()-1].Points.push_back(DPoint(double(newToolPos.X)/scaleFactor,double(newToolPos.Y)/scaleFactor));

					// apend gyro
					gyro.push_back(newToolDir);
					gyro.erase(gyro.begin());
					CheckReportProgress(progressPaths);
				} else {
					//cerr<<"Break: no cut" << endl;
					break;
				}
				if(reachedBoundary)
					break;
			} /* end of points loop*/

			if(toClearPath.size()>0) {
				// expand cleared
				Perf_ExpandCleared.Start();
				clipof.Clear();
				clipof.AddPath(toClearPath,JoinType::jtRound,EndType::etOpenRound);
				Paths toolCoverPoly;
				clipof.Execute(toolCoverPoly,toolRadiusScaled+1);
				clip.Clear();
				clip.AddPaths(cleared,PolyType::ptSubject,true);
				clip.AddPaths(toolCoverPoly,PolyType::ptClip,true);
				clip.Execute(ClipType::ctUnion,cleared);
				CleanPolygons(cleared);
				toClearPath.clear();
				Perf_ExpandCleared.Stop();
			}
			if(cumulativeCutArea>MIN_CUT_AREA_FACTOR*stepScaled*stepOverFactor*referenceCutArea) {
				Path cleaned;
				CleanPath(passToolPath,cleaned,CLEAN_PATH_TOLERANCE);
				total_output_points+=cleaned.size();
				AppendToolPath(output,cleaned,cleared);
				CheckReportProgress(progressPaths);	
			}
			/*****NEXT ENGAGE POINT******/
			if(firstEngagePoint) {
				engage.moveToClosestPoint(newToolPos,stepScaled+1);
				firstEngagePoint=false;
			} else {
				double moveDistance = ENGAGE_SCAN_DISTANCE_FACTOR * stepOverFactor * toolRadiusScaled+1;
				if(!engage.nextEngagePoint(this, cleared,moveDistance,ENGAGE_AREA_THR_FACTOR*optimalCutAreaPD*moveDistance, 
					2*optimalCutAreaPD*moveDistance)) break;
			}
			toolPos = engage.getCurrentPoint();
			toolDir = engage.getCurrentDir();
		}

		/**********************************/
		/*  FINISHING PASS                */
		/**********************************/
		clipof.Clear();
		clipof.AddPaths(boundPaths,JoinType::jtRound,EndType::etClosedPolygon);
		Paths finishingPaths;
		clipof.Execute(finishingPaths,-toolRadiusScaled);
		IntPoint lastPoint;
		for(auto & pth: finishingPaths) {
			Path cleaned;
			CleanPath(pth,cleaned,FINISHING_CLEAN_PATH_TOLERANCE);
			AppendToolPath(output,cleaned,cleared,true);
			if(pth.size()>0) {
				lastPoint.X = pth[pth.size()-1].X;
				lastPoint.Y = pth[pth.size()-1].Y;
			}
		}

		output.ReturnMotionType = CheckCollision(lastPoint, entryPoint,cleared) ? MotionType::mtLinkClear : MotionType::mtLinkNotClear;

		// dump performance results
		#ifdef DEBUG_VISUALIZATION
		Perf_ProcessPolyNode.Stop();
		Perf_ProcessPolyNode.DumpResults();
		Perf_PointIterations.DumpResults();
		Perf_CalcCutArea.DumpResults();
		Perf_NextEngagePoint.DumpResults();
		Perf_ExpandCleared.DumpResults();
		Perf_DistanceToBoundary.DumpResults();
		#endif
		CheckReportProgress(progressPaths);
		double duration=((double)(clock()-start_clock))/CLOCKS_PER_SEC;
		cout<<"PolyNode perf:"<< perf_total_len/double(scaleFactor)/duration << " mm/sec"
			<< " processed_points:" << total_points
			<< " output_points:" << total_output_points
			<< " total_iterations:" << total_iterations
			<< " iter_per_point:" << (double(total_iterations)/((double(total_points)+0.001)))
			<< " total_exceeded:" << total_exceeded  <<  " (" << 100 * double(total_exceeded)/double(total_points) << "%)"
			<< endl;
		results.push_back(output);
	}

}