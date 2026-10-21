// api.js - the only file that talks to the C++ server.
// The browser sends requests; the C++ program owns the classroom array and answers.

async function request(path, options = {}) {
  let response;
  try {
    response = await fetch(path, options);
  } catch {
    throw new Error('Cannot reach the C++ server. Start it with build-and-run.bat, then open http://localhost:8080');
  }
  let data = null;
  try { data = await response.json(); } catch { /* no JSON body */ }
  if (!response.ok) throw new Error((data && data.error) || `Server error (${response.status})`);
  return data;
}

function toForm(room) {
  return new URLSearchParams({
    name: room.name,
    building: room.building,
    floor: room.floor === null ? '' : String(room.floor),
    capacity: String(room.capacity),
    available: String(room.available),
    facilities: room.facilities.join(','),
    description: room.description
  });
}

const formHeaders = { 'Content-Type': 'application/x-www-form-urlencoded' };

export const api = {
  list: (filters) => request(`/api/classrooms?${new URLSearchParams(filters)}`),
  summary: () => request('/api/summary'),
  get: (id) => request(`/api/classrooms/${encodeURIComponent(id)}`),
  create: (room) => request('/api/classrooms', { method: 'POST', headers: formHeaders, body: toForm(room) }),
  update: (id, room) => request(`/api/classrooms/${encodeURIComponent(id)}`, { method: 'PUT', headers: formHeaders, body: toForm(room) }),
  remove: (id) => request(`/api/classrooms/${encodeURIComponent(id)}`, { method: 'DELETE' }),
  searchQueue: () => request('/api/search-queue'),
  enqueueSearch: (criteria) => request('/api/search-queue', { method: 'POST', headers: formHeaders, body: new URLSearchParams(criteria) }),
  processNextSearch: () => request('/api/search-queue', { method: 'DELETE' }),
  reset: () => request('/api/reset', { method: 'POST' })
};
