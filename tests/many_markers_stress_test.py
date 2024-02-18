import struct
import math

path = "output/many_markers_stress_test.DI"
amount = 1000000  # amount of markers to generate
size = 2000  # size of the area to generate markers
offset = (0, 105)

# ChatGPT
# Define the function to calculate the row and column indices
def calculate_indices(amount):
    rows_cols = int(math.sqrt(amount))
    return [(i // rows_cols, i % rows_cols) for i in range(amount)]


# struct DIPoint {
# 	float x, y;
# }
# struct DIFile {
# 	uint32_t length;
# 	DIPoint points[];
# };
# Open the file and write the data
with open(path, "wb") as file:
    file.write(struct.pack('i', amount))  # write length
    indices = calculate_indices(amount)
    for row, col in indices:
        x = row * (size / math.sqrt(amount)) + offset[0]
        y = col * (size / math.sqrt(amount)) + offset[1]
        file.write(struct.pack('f', x))
        file.write(struct.pack('f', y))