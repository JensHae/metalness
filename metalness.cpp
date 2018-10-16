/// @file This file computes a table of settings for the VRayMtl that match various metals based on
/// data from https://refractiveindex.info.
///
/// To build the code, assuming that you have V-Ray Next for 3ds Max 2019 and MSVS 2017, open a MSVS command-line
/// prompt (x64 native tools command-line prompt), go to the folder where this file is located and type
///
/// cl metalness.cpp /I "c:\Program Files\Chaos Group\V-Ray\3ds Max 2019\include" /link kernel32.lib user32.lib gdi32.lib

#include <windows.h>
#include <stdio.h>

#include <math.h>

#include "utils.h"
#include "misc_ray.h"

using namespace VUtils;

int bwidth=800;
int bheight=800;

HINSTANCE hInst; // this process' instance
HWND hWndMain; // handle of main window

RGB32 *buf=NULL;

void putPixel(float x, float y, const Color &c) {
	int xs=fast_floor(x*bwidth);
	int ys=bheight-1-fast_floor(y*(bheight-1));

	if (xs<0 || xs>=bwidth)
		return;

	if (ys<0 || ys>=bheight)
		return;

	buf[ys*bwidth+xs]=c.toRGB32();
}

/// The formula that the VRayMtl material uses to compute metallic Fresnel.
/// @param base The base color.
/// @param reflection The reflection color.
/// @param ior The index of refraction.
/// @param cs The cosine between the viewing angle and the surface normal.
/// @return The reflection strength.
Color getVRayMetallicFresnel(const Color &base, const Color &reflection, float ior, float cs) {
	const simd::Vector3f viewDir(sqrtf(1.0f-cs*cs), 0.0f, -cs);
	const simd::Vector3f normal(0.0f, 0.0f, 1.0f);

	bool internalRefl=false;
	const simd::Vector3f refractDir=getRefractDir(viewDir, normal, ior, internalRefl);

	const float f=getFresnelCoeff(viewDir, normal, refractDir, ior);
	return base*(1.0f-f)+reflection*f;
}

float n_min(float r) { 
   return (1-r)/(1+r);
}

float n_max(float r) {
return (1+sqrt(r))/(1-sqrt(r)); 
}

float get_n(float r, float g) {
   return n_min(r)*g + (1-g)*n_max(r);
}

float get_k2(float r, float n) {
   float nr = (n+1)*(n+1)*r-(n-1)*(n-1);
   return nr/(1-r ); 
}

float get_r(float n, float k) {
   return ((n-1)*(n-1)+k*k)/((n+1)*(n+1)+k*k);
}

float get_g(float n, float k) {
   float r = get_r(n,k);
   return (n_max(r)-n)/(n_max(r)-n_min(r)); 
}

/// Compute artist-friendly reflection strength from base and grazing angle strength. Works by trying
/// to estimate the n and k values with some plausible formula, and then using those n and k values to
/// compute the Fresnel effect.
/// @param r The base reflection strength (when looking directly at the surface along the normal).
/// @param g The reflection strength at 90 degrees.
/// @param c The cosine between the viewing direction and the surface normal.
/// @return A suitable reflection strength.
float olefresnel(float r, float g, float c) { 
   // clamp parameters
   float _r = clamp(r, 0.0f, 0.99f); 
   // compute n and k
   float n = get_n(_r,g);
   float k2 = get_k2(_r,n);

   float rs_num = n*n + k2 - 2*n*c + c*c; 
   float rs_den = n*n + k2 + 2*n*c + c*c;
   float rs = rs_num/rs_den;
   
   float rp_num = (n*n + k2)*c*c - 2*n*c + 1; 
   float rp_den = (n*n + k2)*c*c + 2*n*c + 1;
   float rp = rp_num / rp_den ;
   
   return 0.5f*(rs+rp);
}

/// Artist-Friendly Metallic Fresnel by Ole Gulbrandsen, for comparison purposes against
/// our implementation. Works by trying to estimate the n and k values with some plausible
/// formula and then using those n and k values to compute the Fresnel effect. See
/// http://jcgt.org/published/0003/04/03/paper.pdf for more information.
/// @param base The base color.
/// @param reflection The grazing angle reflection color (usually white).
/// @param cos_theta Angle between the viewing direction and the surface normal.
Color getOleMetallicFresnel(const Color &base, const Color &reflection, float cos_theta) {
	Color result = Color(
      olefresnel(base.r, reflection.r, cos_theta),
      olefresnel(base.g, reflection.g, cos_theta),
      olefresnel(base.b, reflection.b, cos_theta)
   );
	return result;
}

/// Compute reflection strength from complex index of refraction for one wavelength.
/// @param n The n value.
/// @param k The k value.
/// @param c The cosine between the viewing direction and the surface normal.
float complexFresnel(float n, float k, float c) {
	float k2=k*k;
	float rs_num = n*n + k2 - 2*n*c + c*c;
	float rs_den = n*n + k2 + 2*n*c + c*c;
	float rs = rs_num/ rs_den ;

	float rp_num = (n*n + k2)*c*c - 2*n*c + 1;
	float rp_den = (n*n + k2)*c*c + 2*n*c + 1;
	float rp = rp_num/ rp_den ;

	return clamp(0.5f*(rs+rp), 0.0f, 1.0f);
}

/// Complex Fresnel for color n and k values for three wavelengths.
/// @param n The n values.
/// @param k The k values.
/// @param cs The cosine between the viewing direction and the surface normal.
Color getComplexFresnel(const Color &n, const Color &k, float cs) {
	Color result(
		complexFresnel(n[0], k[0], cs),
		complexFresnel(n[1], k[1], cs),
		complexFresnel(n[2], k[2], cs)
	);
	return result;
}

/// A metal preset with n and k values for three wavelengths (0.65, 0.55, 0.45 micrometers).
struct MetalPreset {
	char name[512]; ///< The name of the preset.
	Color n, k; ///< n and k values for red/green/blue wavelengths (0.65, 0.55, 0.45 micrometers).
};

enum MetalPresetName {
	metalPreset_silver=0,
	metalPreset_gold,
	metalPreset_copper,
	metalPreset_aluminum,
	metalPreset_chromium,
	metalPreset_lead,
	metalPreset_platinum,
	metalPreset_titanium,
	metalPreset_tungsten,
	metalPreset_iron,
	metalPreset_vanadium,
	metalPreset_zinc,
	metalPreset_nickel,
	metalPreset_mercury,
	metalPreset_cobalt,

	metalPreset_last,
};

/// Some presets derived from https://refractiveindex.info by sampling the n and k values
/// at 0.65, 0.55, 0.45 micrometers.
MetalPreset metalPresets[metalPreset_last]={
	{ "Silver", Color(0.052225f, 0.059582f, 0.040000f), Color(4.4094f, 3.5974f, 2.6484f) },
	{ "Gold", Color(0.15557f, 0.42415f, 1.3831f), Color(3.6024f, 2.4721f, 1.9155f) },
	{ "Copper", Color(0.23780f, 1.0066f, 1.2404f), Color(3.6264f, 2.5823f, 2.3929f) },
	{ "Aluminum", Color(1.5580f, 1.0152f, 0.63324f), Color(7.7124f, 6.6273f, 5.4544f) },
	{ "Chromium", Color(3.1071f, 3.1812f, 2.3230f), Color(3.3314f, 3.3291f, 3.1350f) },
	{ "Lead", Color(2.5750f, 2.5444f, 2.1038f), Color(4.1612f, 4.1823f, 4.1890f) },
	{ "Platinum", Color(0.47475f, 0.46521f, 0.63275f), Color(6.3329f, 5.1073f, 3.7481f) },
	{ "Titanium", Color(0.25300f, 0.28822f, 0.52181f), Color(5.2796f, 4.2122f, 3.0367f) },
	{ "Tungsten", Color(0.92074f, 1.3437f, 2.2323f), Color(6.8595f, 5.2293f, 5.1461f) },
	{ "Iron", Color(1.8247f, 1.2246f, 1.0205f), Color(7.6326f, 5.9377f, 4.3952f) },
	{ "Vanadium", Color(0.43109f, 0.60711f, 0.91187f), Color(5.5575f, 4.5217f, 3.6035f) },
	{ "Zinc", Color(1.2338f, 0.92943f, 0.67767f), Color(5.8730f, 4.9751f, 4.0122f) },
	{ "Nickel", Color(1.3726f, 1.0753f, 1.1336f), Color(6.6273f, 5.1763f, 3.7544f) },
	{ "Mercury", Color(2.0733f, 1.5523f, 1.0606f), Color(5.3383f, 4.6510f, 3.8628f) },
	{ "Cobalt", Color(2.2371f, 2.0524f, 1.7365f), Color(4.2357f, 3.8242f, 3.2745f) }
};

/// Given n and k values, find the best VRayMtl IOR value that will give the closest match to the
/// actual complex reflectance curve.
/// @param n The n values for red/green/blue.
/// @param k The k values for red/green/blue.
/// @return An IOR value for the VRayMtl material that is the closest fit to the actual
/// complex reflectance curve. Computed by sampling all IOR values between 1.001f and 10.0f,
/// and for each IOR value, computing the difference between the VRayMtl metallic Fresnel reflectance
/// curve, and the actual complex reflectance curve.
float findIOR(const Color &n, const Color &k) {
	float bestIOR=-1.0f;
	float bestResult=1e18f;

	// Compute the reflectance at 90 degrees.
	Color reflection=getComplexFresnel(n, k, 0.0f);
	
	// Compute the reflectance when looking directly at the surface along the normal.
	Color base=getComplexFresnel(n, k, 1.0f);

	// Step through all IOR values between 1.001f and 10.0f and find the best match.
	// For each value, sample the VRayMtl metallic reflectance curve and the actual complex
	// Fresnel reflectance curve for different viewing angles and compute the average difference.
	// The best IOR value is the one with minimal differences between the VRayMtl curve and the
	// actual complex Fresnel curve.
	for (float ior=1.001f; ior<10.0f; ior+=0.001f) {
		double sum=0.0f;
		for (int xs=1; xs<200; xs++) {
			// Compute a viewing angle.
			float x=(float) xs/(float) 200;

			// Compute the VRayMtl reflectance
			Color vrayMetallicFresnel=getVRayMetallicFresnel(base, reflection, ior, x);
			
			// Compute the actual complex Fresnel reflectance.
			Color complexFresnel=getComplexFresnel(n, k, x);

			// Accumulate the difference.
			sum+=(vrayMetallicFresnel-complexFresnel).lengthSqr();
		}

		// If result is better than what we have so far, save it.
		if (sum<bestResult) {
			bestResult=float(sum);
			bestIOR=ior;
		}
	}

	// Return the best result that we found.
	return bestIOR;
}

void putColorGraph(float x, const Color &c, float f) {
	putPixel(x, c.r, Color(1.0f, f, f));
	putPixel(x, c.g, Color(f, 1.0f, f));
	putPixel(x, c.b, Color(f, f, 1.0f));
}

/// Go through all metalPresets and fill in a CSV file with the computed IOR values and average errors
/// between the actual complex Fresnel curve and the VRayMtl metallic Fresnel vs Old Gulbrandsen's metallic Fresnel
/// respectively. Also draws the reflectance curves for the actual complex Fresnel, the VRayMtl metallic Fresnel
/// and the artist-friendly metallic Fresnel version by Ole Gulbrandsen.
DWORD WINAPI renderCycle(LPVOID param) {
	HWND hWnd=(HWND) param;

	RGB32 *cbuf=new RGB32[bwidth*bheight];
	buf=cbuf;

	// A CSV file for the results. Change the path as needed.
	FILE *fp=fopen("d:/temp/metal_presets.csv", "wt");
	if (fp) fprintf(fp, "Name, Diffuse red, Diffuse green, Diffuse blue, Reflection red, Reflection green, Reflection blue, IOR, Color (web sRGB), V-Ray error, Ole error\n");

	Color legend(0.1f, 0.09f, 0.08f);

	for (int presetIdx=0; presetIdx<metalPreset_last; presetIdx++) {
		FillMemory(cbuf, sizeof(RGB32)*bwidth*bheight, 0x00);

		Color n=metalPresets[presetIdx].n;
		Color k=metalPresets[presetIdx].k;

		// The 90 degrees reflection color for the n and k values.
		Color reflection=getComplexFresnel(n, k, 0.0f);
		
		// The base reflection color when looking directly at the surface along the normal.
		Color base=getComplexFresnel(n, k, 1.0f);

		// Find an IOR value for the VRayMtl material for these n and k values.
		float ior=findIOR(n, k);
		
		// Compute the base color in sRGB display color space so that colors can be picked from
		// the web page, f.e. with the 3ds Max color picker tool, which will do the inverse sRGB conversion
		// automatically.
		Color base_sRGB=base;
		base_sRGB.encodeToSRGB();

		bool fastQuit=false;
		double oleErrorSqr=0.0f; // Error between the actual complex Fresnel curve and the Ole version.
		double vrayErrorSqr=0.0f; // Error between the actual complex Fresnel curve and the VRayMtl version.

		// Draw graphs of the actual complex Fresnel reflectance, the Ole version and the VRayMtl version.
		int N=(bwidth*2);
		for (int xs=1; xs<N; xs++) {
			float x=float(xs)/float(N);

			// Get the VRayMtl metallic Fresnel value based on the computed best IOR.
			Color vrayMetallicFresnel=getVRayMetallicFresnel(base, reflection, ior, x);
			
			// Get the Ole metallic Fresnel version based only on the colors.
			Color oleMetallicFresnel=getOleMetallicFresnel(base, reflection, x);

			// Get the actual complex Fresnel value based on the n and k values.
			Color complexFresnel=getComplexFresnel(n, k, x);

			// Plot the VRayMtl metallic Fresnel with solid graphs for red/green/blue.
			putColorGraph(x, vrayMetallicFresnel, 0.6f);
			if (xs>10 && xs<110) {
				putColorGraph(x, legend, 0.6f);
			}

			// Plot the Ole metallic Fresnel with short dashed graphs for red/green/blue
			if (((xs/6)&1)==0) {
				putColorGraph(x, oleMetallicFresnel, 0.4f);
				if (xs>510 && xs<610) {
					putColorGraph(x, legend, 0.4f);
				}
			}

			// Plot the actual complex Fresnel reflectance with long dashed graphs for red/green/blue
			if (((xs/20)&1)==0) {
				putColorGraph(x, complexFresnel, 0.0f);
				if (xs>910 && xs<1010) {
					putColorGraph(x, legend, 0.0f);
				}
			}

			// Accumulate the average error for the VRayMtl and Ole versions.
			vrayErrorSqr+=(vrayMetallicFresnel-complexFresnel).lengthSqr();
			oleErrorSqr+=(oleMetallicFresnel-complexFresnel).lengthSqr();
		}

		// Compute the average errors.
		vrayErrorSqr/=float(N);
		oleErrorSqr/=float(N);

		double vrayError=sqrt(vrayErrorSqr);
		double oleError=sqrt(oleErrorSqr);

		// Print the data into the CSV file.
		if (fp) fprintf(
			fp,
			"%s, %g, %g, %g, %g, %g, %g, %g, %x, %g, %g\n",
			metalPresets[presetIdx].name,
			floorf(base.r*255.0f), floorf(base.g*255.0f), floorf(base.b*255.0f),
			floorf(reflection.r*255.0f), floorf(reflection.g*255.0f), floorf(reflection.b*255.0f),
			ior,
			int(base_sRGB.toRGB32()),
			vrayError, oleError
		);

		InvalidateRect(hWnd, NULL, FALSE);
		PostMessage(hWnd, WM_PAINT, 0, 0);
		
		// Wait a bit so that the graph is visible before moving on to the next preset.
		msSleep(100);
	}

	if (fp)
		fclose(fp);

	return 0;
}

LRESULT WINAPI mainWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
		case WM_CLOSE:
		case WM_DESTROY:
			// PostThreadMessage(renderThreadID, WM_QUIT, 0,0);
			// WaitForSingleObject(hRenderThread, 1000*60); // for at most one minute for the rendering thread to terminate
			PostQuitMessage(0);
			return 0;
		case WM_PAINT: {
			PAINTSTRUCT ps;
			HDC hdc=BeginPaint(hWnd, &ps);
			if (!buf) {
				RECT crect;
				GetClientRect(hWnd, &crect);
				HBRUSH brush=CreateSolidBrush(RGB(0,0,0));
				HANDLE oldbrush=SelectObject(hdc, brush);
				FillRect(hdc, &crect, brush);
				SelectObject(hdc, oldbrush);
				DeleteObject(brush);
			} else {
				static BITMAPINFO bmpInfo={{sizeof(BITMAPINFOHEADER), bwidth, -bheight, 1, 32, BI_RGB, 0, 0, 0, 0, 0}, NULL};
				SetDIBitsToDevice(hdc, 0,0, bwidth, bheight, 0, 0, 0, bheight, buf, &bmpInfo, DIB_RGB_COLORS);
			}
			EndPaint(hWnd, &ps);
			return 0;
		}
		case WM_SETCURSOR:
			SetCursor(LoadCursor(NULL, IDC_ARROW));
			return 0;
	}
	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

HANDLE hRenderThread;
DWORD renderThreadID;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdLine, int nCmdShow) {
	UNREFERENCED_PARAMETER(lpszCmdLine);  

	hInst=hInstance;

	WNDCLASS wc;
	wc.style=CS_OWNDC;
	wc.lpfnWndProc=mainWndProc;
	wc.cbClsExtra=0;
	wc.cbWndExtra=0;
	wc.hInstance=hInst;
	wc.hIcon=NULL; // LoadIcon(hInst, MAKEINTRESOURCE(IDI_ICON1)); // (char*) icon_MAIN);
	wc.hCursor=LoadCursor(hInst, IDC_CROSS);
	wc.hbrBackground=NULL; // GetStockObject(WHITE_BRUSH); 
	wc.lpszMenuName=NULL; 
	wc.lpszClassName="MainWndClass";

	if (!RegisterClass(&wc)) return FALSE;

	RECT rect={0,0,bwidth,bheight};
	int winStyle=WS_CAPTION | WS_SYSMENU;
	AdjustWindowRect(&rect, winStyle, FALSE); // FALSE=no menu
	hWndMain=CreateWindow("MainWndClass", "Interpolate",
		winStyle,
		CW_USEDEFAULT, CW_USEDEFAULT,
		rect.right-rect.left, rect.bottom-rect.top,
		(HWND) NULL, (HMENU) NULL, hInst, (LPVOID) NULL);

	if (!hWndMain) return FALSE;

	ShowWindow(hWndMain, nCmdShow);
	UpdateWindow(hWndMain);

	hRenderThread=CreateThread(NULL, 0, renderCycle, (LPVOID) hWndMain, 0, &renderThreadID);

	MSG msg;
	while(GetMessage(&msg, (HWND) NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	PostThreadMessage(renderThreadID, WM_QUIT, 0, 0);
	WaitForSingleObject(hRenderThread, 10000);
	delete[] buf;

	CloseHandle(hRenderThread);
	return int(msg.wParam);
}  
