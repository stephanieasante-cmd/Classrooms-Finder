# UCC Classroom Availability Finder (C++ backend + HTML front end)

A web page (HTML/CSS/JavaScript) that shows classrooms. The C++ backend uses a fixed-size array for classroom records and a FIFO queue for a student waiting list.

## How it fits together

```
Browser (frontend/)  --- HTTP requests --->  C++ server (backend/)
  index.html, CSS, JS   <--- JSON answers ---  Classroom rooms[MAX_ROOMS]  (the array)
                                                    |
                                              data/classrooms.txt  (saved between runs)
```

- `backend/classroom_store.h/.cpp` - the classroom array and its operations, written with loops.
- `backend/main.cpp` - also implements the FIFO waiting queue using `std::queue<WaitRequest>`. The queue is held in memory while the server runs.
- `backend/main.cpp` - a small web server. It turns browser requests into calls on the array and sends the answer back as JSON. It also serves the files in `frontend/`.
- `frontend/` - the user interface. It only displays data; it never searches or sorts.
- `tests/test_store.cpp` - tests for the array logic.

## Where the arrays are used

| Task | How it is done in `classroom_store.cpp` |
|---|---|
| Store all rooms | `Classroom rooms[MAX_ROOMS]` plus a `count` of used slots |
| Search by ID | `findIndexById`: linear search with a `for` loop |
| Add a room | `addRoom`: put it in `rooms[count]`, then `count++` |
| Delete a room | `removeRoom`: shift later rooms one step left, then `count--` |
| Update a room | `updateRoom`: find the index, overwrite that slot |
| Search and filter | `filterRooms`: loop over the array and collect matching positions |
| Sort | `sortIndexes`: insertion sort |
| Totals | `getStats`: one loop adding up counts and seats |
| Unique buildings | `getBuildings`: nested loops, then insertion sort |

## Run it (Windows, MSYS2 UCRT64)

1. Double-click `build-and-run.bat`. It compiles the server and opens http://localhost:8080.
2. Keep the black window open while you use the page. Press Ctrl+C in it to stop.
3. Run the tests with `run-tests.bat`.

Opening `index.html` directly from the folder will not work, because the page needs the C++ server.

## Data

The 12 rooms are fictional sample data (not live university data). On the first run the server creates `data/classrooms.txt` from them. Add, edit and delete in the page are saved there. "Reset sample data" restores the original 12 rooms.

## Queue implementation (for the presentation)

The project uses `#include <queue>` and `std::queue<WaitRequest> waitingQueue`. Each queue entry stores a student's name and the classroom ID/name they are waiting for. The queue follows FIFO (First In, First Out): `push()` adds at the back, `front()` reads the first entry, and `pop()` removes the first entry. This waiting list is in memory and is cleared when the server restarts or sample data is reset.

Queue API routes:

- `GET /api/waitlist` - lists the waiting queue in FIFO order.
- `POST /api/waitlist` - add a student; form fields: `studentName` and `roomId`.
- `DELETE /api/waitlist` - serve/remove the first student in the queue; returns HTTP 409 if empty.

Example form body for POST: `studentName=Student%20A&roomId=ROOM-001`.

## API (for the presentation)

| Request | What it does |
|---|---|
| `GET /api/classrooms?query=&availability=&building=&groupSize=&sort=` | filtered and sorted list |
| `GET /api/classrooms/ROOM-001` | one room (linear search) |
| `POST /api/classrooms` | add a room |
| `PUT /api/classrooms/ROOM-001` | update a room |
| `DELETE /api/classrooms/ROOM-001` | delete a room |
| `GET /api/summary` | totals and building names |
| `POST /api/reset` | restore sample data |

## Deploying online (Render, using Docker)

The `Dockerfile` compiles the C++ server and runs it together with the `frontend` folder. The server reads the `PORT` setting from the host and listens on all addresses when `PORT` is set.

1. Push this folder to a GitHub repository (the `Dockerfile` must be at the top level).
2. Sign in to render.com with GitHub.
3. Click New > Web Service and choose your repository.
4. Choose **Docker** as the language and the **Free** instance type, then click Create Web Service.
5. Wait for the build to finish. Render gives you a public link ending in `.onrender.com`.

Notes for the demo:
- On a free instance the site goes to sleep when idle, so open the link a minute before presenting.
- A free instance has no permanent disk. If the service restarts, `data/classrooms.txt` is lost and the 12 sample rooms come back.
- There is no login, so anyone with the link can add or delete rooms. This is a demonstration only.

To test the Docker image on your own computer (needs Docker Desktop):
`docker build -t ucc-finder .` then `docker run -p 10000:10000 ucc-finder` and open http://localhost:10000


## Classroom search queue (FIFO)

The website includes a **Search queue** section. It queues classroom-search criteria—not student registrations or bookings. Add criteria such as a keyword, building, availability, minimum seats, and sort order. The C++ backend stores each request in `std::queue<SearchRequest>`.

- `POST /api/search-queue` adds a search request (`push`).
- `GET /api/search-queue` shows requests in FIFO order by inspecting a copy.
- `DELETE /api/search-queue` processes the oldest request (`front`, then `pop`), searches the classroom array, and returns matching rooms.

The queue is in memory and is cleared when the server restarts. It demonstrates FIFO processing for the DSA assignment; normal interactive classroom searches on the directory still run immediately. The queue does not reserve rooms or change availability.
