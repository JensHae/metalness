# Metalness
The code that I used to derive parameters for various metals for the VRayMtl material based on data from https://refractiveindex.info. This program was used to produce the table for our metalness blog post at https://www.chaosgroup.com/blog/understanding-metalness

# Build instructions

See the start of the metalness.cpp file.

# Example output

The folder example_ouptut contains files with example output from the program. As can be seen, the VRayMtl version of the metalness along with the IOR values from this program produce results that are closer to the actual complex Fresnel curve compared to Ole Gulbrandsen's "Artist-Friendly Metallic Fresnel" (see http://jcgt.org/published/0003/04/03/paper.pdf)
