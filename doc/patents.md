List of Vivante patents
=========================

List of relevant Vivante Corporation patents in reverse chronological order by publication date.
Although these describe the hardware implementation and not the interface to the hardware,
the patent applications can be a useful source of information as to why certain things
are done in the way that they are. For example how clipping happens, what some of the
performance counters mean, what the modules (SE, RA, PA) do, and so on.

- [US20130097212](https://www.google.com/patents/US20130097212) Low Power and Low Memory Single-Pass
Multi-Dimensional Digital Filtering

Published: 2013-04-18

Disclosed are new approaches to Multi-dimensional filtering with a reduced number of memory reads
and writes. In one embodiment, a filter includes first and second coefficients. A block of a data
having width and height each equal to the number of one of the first or second coefficients is read
from a memory device. Arrays of values from the block are filtering using the first filter
coefficients and the results filtered using the second coefficients. The final result may be
optionally blended with another data value and written to a memory device. Registers store results
of filtering with the first coefficients. The block of data may be read from a location including a
source coordinate. The final result of filtering may be written to a destination coordinate obtained
by rotating and/or mirroring the source coordinate. The orientation of arrays filtered using the
first coefficients varies according to a rotation mode.

- [US20130091189](https://www.google.com/patents/US20130091189) Single datapath floating point implementation of RCP, SQRT, EXP and LOG functions
and a low latency RCP based on the same techniques

Published: 2013-04-11

Methods and apparatus is provided for computing mathematical functions comprising a single pipeline
for performing a polynomial approximation (e.g. a quadratic polynomial approximation, or the like);
and one or more data tables corresponding to at least one of the RCP, SQRT, EXP or LOG functions
operable to be coupled to the single pipeline according to one or more opcodes; wherein the single
pipeline is operable for computing at least one of RCP, SQRT, EXP or LOG functions according to the
one or more opcodes.

- [US20130002651](https://www.google.com/patents/US20130002651) Apparatus and Method For Texture Level Of Detail Computation

Published: 2013-01-03

A graphic processing system to compute a texture level of detail. An embodiment of the graphic
processing system includes a memory device, a driver, and level of detail computation logic. The
memory device is configured to implement a first lookup table. The first lookup table is configured
to provide a first level of detail component. The driver is configured to calculate a log value of a
second level of detail component. The level of detail computation logic is coupled to the memory
device and the driver. The level of detail computation logic is configured to compute a level of
detail for a texture mapping operation based on the first level of detail component from the lookup
table and the second level of detail component from the driver. Embodiments of the graphic
processing system facilitate a simple hardware implementation using operations other than
multiplication, square, and square root operations.

- [US20130091189](https://www.google.com/patents/US20130091189) Systems and methods for computing mathematical functions

Published: 2013-01-01

Mathematical functions are computed using a single hardware pipeline that performs polynomial
approximation of second degree or higher. The single hardware pipeline includes multiple stages.
Several data tables are used on the computations. The data tables are associated with a reciprocal,
square root, exponential, or logarithm function. The data tables include data associated with
implementing the associated function. The single hardware pipeline computes at least one of the
functions associated with the data tables.

- [US20110249901](https://www.google.com/patents/US20110249901) Anti-Aliasing System and Method

Published: 2011-10-13

A system to reduce aliasing in a graphical image includes an edge detector configured to read image
depth information from a depth buffer. The edge detector also applies edge detection procedures to
detect an object edge within the image. An edge style detector is configured to identify a first
edge end and a second edge end. The edge style detector also identifies an edge style associated
with the detected edge based on the first edge end and the second edge end. The system also includes
a restoration module configured to identify pixel data associated with the detected edge and a
blending module configured to blend the pixel data associated with the detected edge.

- [US20110234609](https://www.google.com/patents/US20110234609) Hierarchical tile-based rasterization algorithm

Published: 2011-09-29

A hierarchical tile-based rasterization method is disclosed. The inventive rasterization algorithm
rasterizes pixels in hierarchical rectangles or blocks. The method includes: walking a plurality of
tiles of pixels and determining if each tile is valid; breaking each valid tile into a plurality of
subtiles and determining if each subtile is valid; breaking each valid subtile into a plurality of
quads and determining if each quad is valid; and rendering pixels for each valid quad. These
hierarchical levels of block validations are performed in parallel. The inventive rasterization
algorithm is further implemented in hardware for better performance.

- [US20100271370](https://www.google.com/patents/US20100271370) Method for distributed clipping outside of view volume

Published: 2010-10-28

A distributed clipping scheme is provided, view frustum culling is distributed in several places in
a graphics processing pipeline to simplify hardware implementation and improve performance. In
general, many 3D objects are outside viewing frustum. In one embodiment, clipping is performed on
these objects with a simple algorithm in the PA module, such as near Z clipping, trivial rejection
and trivial acceptance. In one embodiment, the SE and RA modules perform the rest of clipping, such
as X, Y and far Z clipping.  In one embodiment, the SE module performs clipping by way of computing
a initial point of rasterization. In one embodiment, the RA module performs clipping by way of
conducting the rendering step of the rasterization process. This approach distributes the complexity
in the graphics processing pipeline and makes the design simpler and faster, therefore design
complexity, cost and performance may all be improved in hardware implementation.

- [US20100131786](https://www.google.com/patents/US20100131786) Single Chip 3D and 2D Graphics Processor with Embedded Memory and Multiple Levels of
Power Controls

Published: 2010-05-27

An apparatus and method is provided for data processing where power is automatically controlled with
a feed back loop with the host processor based on the internal work load characterized by
performance counters. The host automatically adjusts internal frequencies or voltage level to match
the work load. The feedback loop allows tuning of frequency or voltage controlling power
dissipation.

- [EP2117233A1](https://www.google.com/patents/EP2117233A1) De-ringing filter for decompressed video data

Published: 2009-11-11

A post processing apparatus of a graphics controller to filter decompressed video data. An
embodiment of the apparatus includes a buffer and a de-ringing filter. The buffer is configured to
read a pixel line of video data from memory. The pixel line includes pixels from adjacent
macroblocks of the video data. The de-ringing filter is coupled to the buffer. The de-ringing filter
is configured to identify a maximum pixel jump between adjacent pairs of pixels in the pixel line
and to apply a de-ringing filter to a pixel within a pixel subset of the pixel line in response to a
determination that the pixel is not an edge pixel. The determination that the pixel is not an edge
pixel is based on the identified maximum pixel jump.

- [US20090122076](https://www.google.com/patents/US20090122076) Thin-line detection apparatus and method

Published: 2009-05-14

An apparatus and method for detecting and handling thin lines in a raster image includes reading
depth values for each pixel of an n×m block of pixels surrounding a substantially central pixel.
Differences are then calculated for selected depth values of the n×m block of pixels to yield
multiple difference values. These difference values may then be compared with multiple pre-computed
difference values associated with thin lines pre-determined to pass through the n×m block of
pixels. If the difference values of the pixel block substantially match the difference values of one
of the pre-determined thin lines, the pixel block may be deemed to describe a thin line. The
apparatus and method may preclude application of an anti-aliasing filter to the substantially
central pixel of the pixel block in the event it describes a thin line.

- [US20090122068](https://www.google.com/patents/US20090122068) Intelligent configurable graphics bandwidth modulator

Published: 2009-05-14

An apparatus and method to dynamically regulate system bandwidth in a graphics system includes
receiving vertex data from an application by way of an application programming interface. The rate
that the vertex data is received from the application is then determined. In the event the rate is
greater than a selected threshold, the graphics system is configured to operate in immediate mode,
wherein vertex data is rendered immediately upon reception. In the event the rate is less than the
selected threshold, the graphics system is configured to operate in retained mode, wherein vertex
data is stored prior to being rendered. The apparatus and method switches between each of the modes
on-the-fly in a manner that is transparent to the application.

- [US20090122064](https://www.google.com/patents/US20090122064) Efficient tile-based rasterization

Published: 2009-05-14

An apparatus and method for rasterizing a primitive in a graphics system is disclosed in one example
of the invention as including scanning a first row of tiles, one tile at a time, starting from a
first point and scanning in a first direction. Immediately after scanning the first row of tiles,
the method includes moving from the first point to a second point in an orthogonal direction
relative to the first row. Immediately after moving from the first point to the second point, the
method includes scanning a second row of tiles, one tile at a time, starting from the second point
and scanning in the first direction. By scanning rows in the same direction immediately prior to and
after moving from one row to another, cache utilization is improved.

- [US20080276066](https://www.google.com/patents/US20080276066) Virtual memory translation with pre-fetch prediction

Published: 2008-11-06

A system to facilitate virtual page translation. An embodiment of the system includes a processing
device, a front end unit, and address translation logic. The processing device is configured to
process data of a current block of data. The front end unit is coupled to the processing device. The
front end unit is configured to access the current block of data in an electronic memory device and
to send the current block of data to the processor for processing. The address translation logic is
coupled to the front end unit and the electronic memory device. The address translation logic is
configured to pre-fetch a virtual address translation for a predicted virtual address based on a
virtual address of the current block of data. Embodiments of the system increase address translation
performance of computer systems including graphic rendering operations.

- [US20080273043](https://www.google.com/patents/US20080273043) Coordinate computations for non-power of 2 texture maps

Published: 2008-11-06

A graphic processing system to compute a texture coordinate. An embodiment of the graphic processing
system includes a memory device, a texture coordinate generator, and a display device. The memory
device is configured to store a plurality of texture maps. The texture coordinate generator is
coupled to the memory device. The texture coordinate generator is configured to compute a final
texture coordinate using an arithmetic operation exclusive of a division operation. The display
device is coupled to the texture coordinate generator. The display device is configured to display a
representation of one of the plurality of texture maps according to the final texture coordinate.
Embodiments of the graphic processing system facilitate a simple hardware implementation using
operations other than division.

- [US20080252659](https://www.google.com/patents/US20080252659) Post-rendering anti-aliasing with a smoothing filter

Published: 2008-10-16

A system to apply a smoothing filter during anti-aliasing at a post-rendering stage. An embodiment
of the system includes a three-dimensional renderer, an edge detector, and a smoothing filter. The
three-dimensional renderer is configured to render a three-dimensional scene. The edge detector is
coupled to the three-dimensional renderer. The edge detector is configured to read values of a depth
buffer and to apply edge detection criteria to the values of the depth buffer in order to detect an
object edge within the three-dimensional scene. The smoothing filter coupled to the edge detector.
The smoothing filter is configured to read values of a color buffer and to apply a smoothing
coefficient to the values of the color buffer. The values of the color buffer include a pixel sample
at the detected object edge.

