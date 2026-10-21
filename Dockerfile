# Stage 1: compile the C++ server
FROM gcc:13 AS build
WORKDIR /app
COPY backend ./backend
RUN g++ -std=c++17 -O2 -static backend/classroom_store.cpp backend/main.cpp -o server

# Stage 2: small image that only runs the server and serves the page
FROM debian:bookworm-slim
WORKDIR /app
COPY --from=build /app/server ./server
COPY frontend ./frontend
# Hosting services set PORT; 10000 is the default used by Render.
ENV PORT=10000
EXPOSE 10000
CMD ["./server"]
