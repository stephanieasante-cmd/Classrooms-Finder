import { api } from './api.js';

const $ = (selector) => document.querySelector(selector);
const grid = $('#classroom-grid');
const emptyState = $('#empty-state');
const form = $('#classroom-form');
const dialog = $('#room-dialog');
let toastTimer;
let renderToken = 0;   // lets us ignore out-of-date answers from the server

function setStorageStatus(message) {
  $('#storage-status').textContent = message;
}

/** Only checks what the person typed. The searching and filtering happen in C++. */
function parseGroupSize(rawValue) {
  const raw = String(rawValue ?? '').trim();
  if (!raw) return { valid: false, value: null, message: 'Enter a group size to use smart room matching.' };
  const value = Number(raw);
  if (!Number.isFinite(value) || !Number.isInteger(value)) return { valid: false, value: null, message: 'Group size must be a whole number.' };
  if (value <= 0) return { valid: false, value: null, message: 'Group size must be at least 1 person.' };
  if (value > 10000) return { valid: false, value: null, message: 'Group size cannot exceed 10,000 people.' };
  return { valid: true, value, message: '' };
}

function reportError(error) {
  setStorageStatus(error.message);
  showToast(error.message, true);
}

function showToast(message, isError = false) {
  const toast = $('#toast');
  toast.textContent = message;
  toast.classList.toggle('toast-error', isError);
  toast.classList.add('toast-visible');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => toast.classList.remove('toast-visible'), 3200);
}

function formatNumber(value) {
  return new Intl.NumberFormat().format(value);
}

function updateStats(summary) {
  $('#stat-total').textContent = formatNumber(summary.total);
  $('#stat-available').textContent = formatNumber(summary.available);
  $('#stat-unavailable').textContent = formatNumber(summary.unavailable);
  $('#stat-seats').textContent = formatNumber(summary.seats);
  $('#hero-available-count').textContent = formatNumber(summary.available);
}

function updateBuildingOptions(buildings) {
  const select = $('#building-filter');
  const previous = select.value;
  select.replaceChildren(new Option('All buildings', 'all'));
  buildings.forEach((building) => select.add(new Option(building, building)));
  select.value = [...select.options].some((option) => option.value === previous) ? previous : 'all';
}

function makeElement(tag, className, text) {
  const element = document.createElement(tag);
  if (className) element.className = className;
  if (text !== undefined) element.textContent = text;
  return element;
}

function createRoomCard(room) {
  const card = makeElement('article', 'room-card');
  const top = makeElement('div', 'room-card-top');
  const roomIcon = makeElement('div', 'room-icon', '▦');
  roomIcon.setAttribute('aria-hidden', 'true');
  const status = makeElement('span', `status-pill ${room.available ? 'status-available' : 'status-unavailable'}`, room.available ? 'Available' : 'Unavailable');
  top.append(roomIcon, status);
  const building = makeElement('p', 'room-building', room.building);
  const title = makeElement('h3', 'room-name', room.name);
  const meta = makeElement('div', 'room-meta');
  const capacity = makeElement('span', '', `♧  ${room.capacity} seats`);
  const floor = makeElement('span', '', room.floor === null || room.floor === undefined ? 'Floor not set' : `Floor ${room.floor}`);
  meta.append(capacity, floor);
  const facilityList = makeElement('div', 'facility-list');
  room.facilities.slice(0, 3).forEach((facility) => facilityList.append(makeElement('span', 'facility-tag', facility)));
  if (room.facilities.length > 3) facilityList.append(makeElement('span', 'facility-tag', `+${room.facilities.length - 3}`));
  const description = makeElement('p', 'room-description', room.description || 'No description provided.');
  const actions = makeElement('div', 'room-card-actions');
  const details = makeElement('button', 'text-button', 'View details →');
  details.type = 'button';
  details.addEventListener('click', () => openDetails(room.id));
  const edit = makeElement('button', 'icon-button', 'Edit');
  edit.type = 'button';
  edit.setAttribute('aria-label', `Edit ${room.name}`);
  edit.addEventListener('click', () => startEdit(room));
  actions.append(details, edit);
  card.append(top, building, title, meta, facilityList, description, actions);
  return card;
}

async function renderRooms() {
  const token = ++renderToken;
  const rawGroupSize = $('#group-size').value;
  const groupSize = rawGroupSize.trim() === '' ? null : parseGroupSize(rawGroupSize);
  const groupError = $('#group-size-error');
  groupError.textContent = groupSize && !groupSize.valid ? groupSize.message : '';
  const effectiveGroupSize = groupSize && groupSize.valid ? groupSize.value : null;
  const invalidGroup = Boolean(groupSize && !groupSize.valid);

  let rooms = [];
  let total = 0;
  if (!invalidGroup) {
    const filters = {
      query: $('#directory-search').value,
      availability: effectiveGroupSize === null ? $('#availability-filter').value : 'available',
      building: $('#building-filter').value,
      sort: $('#sort-filter').value
    };
    if (effectiveGroupSize !== null) filters.groupSize = String(effectiveGroupSize);
    try {
      const result = await api.list(filters);   // the C++ array does the filtering and sorting
      rooms = result.rooms;
      total = result.total;
    } catch (error) {
      if (token === renderToken) reportError(error);
      return;
    }
    if (token !== renderToken) return;           // a newer request replaced this one
  }

  grid.replaceChildren();
  rooms.forEach((room) => grid.append(createRoomCard(room)));
  $('#results-count').textContent = invalidGroup ? 'Fix the group size to use smart matching.' : `Showing ${rooms.length} of ${total} classroom${total === 1 ? '' : 's'}`;
  $('#matching-label').textContent = effectiveGroupSize ? `Available rooms for ${effectiveGroupSize} ${effectiveGroupSize === 1 ? 'person' : 'people'}` : '';
  emptyState.hidden = rooms.length > 0;
  grid.hidden = rooms.length === 0;
  grid.setAttribute('aria-busy', 'false');
}


function describeSearchRequest(request) {
  const parts = [];
  if (request.query) parts.push(`keyword: "${request.query}"`);
  if (request.building && request.building !== 'all') parts.push(`building: ${request.building}`);
  parts.push(`availability: ${request.availability}`);
  if (request.groupSize > 0) parts.push(`minimum seats: ${request.groupSize}`);
  parts.push(`sort: ${request.sortBy}`);
  return parts.join(' · ');
}

async function renderSearchQueue() {
  const list = $('#queue-list');
  try {
    const data = await api.searchQueue();
    $('#queue-count').textContent = String(data.size);
    list.replaceChildren();
    if (!data.requests.length) { list.append(makeElement('li', 'queue-empty', 'The queue is empty. Add a search request to begin.')); return; }
    data.requests.forEach((request, index) => {
      const item = makeElement('li', 'queue-item');
      const number = makeElement('span', 'queue-position', String(index + 1));
      const content = makeElement('div', 'queue-item-content');
      content.append(makeElement('strong', '', index === 0 ? 'Next to process' : `Queued request ${index + 1}`), makeElement('p', '', describeSearchRequest(request)));
      item.append(number, content); list.append(item);
    });
  } catch (error) {
    list.replaceChildren(makeElement('li', 'queue-empty', `Could not load queue: ${error.message}`));
    $('#queue-result-status').textContent = 'The queue could not be loaded. Check that the C++ server is running.';
  }
}

function renderQueueResults(rooms) {
  const target = $('#queue-results'); target.replaceChildren();
  if (!rooms.length) { target.append(makeElement('p', 'queue-empty', 'No classrooms matched this search request.')); target.hidden = false; return; }
  rooms.forEach((room) => {
    const item = makeElement('article', 'queue-result-card');
    item.append(makeElement('strong', '', room.name), makeElement('span', `status-pill ${room.available ? 'status-available' : 'status-unavailable'}`, room.available ? 'Available' : 'Unavailable'), makeElement('p', '', `${room.building} · ${room.capacity} seats`));
    target.append(item);
  });
  target.hidden = false;
}

async function refresh() {
  try {
    const summary = await api.summary();
    updateStats(summary);
    updateBuildingOptions(summary.buildings);
    const queueBuilding = $('#queue-building');
    if (queueBuilding) {
      const previousQueueBuilding = queueBuilding.value;
      queueBuilding.replaceChildren(new Option('All buildings', 'all'));
      summary.buildings.forEach((building) => queueBuilding.add(new Option(building, building)));
      queueBuilding.value = [...queueBuilding.options].some((option) => option.value === previousQueueBuilding) ? previousQueueBuilding : 'all';
    }
  } catch (error) {
    reportError(error);
    return;
  }
  await renderRooms();
}

async function openDetails(id) {
  let room;
  try {
    room = await api.get(id);   // the C++ server runs a linear search by ID
  } catch (error) {
    showToast(error.message, true);
    refresh();
    return;
  }
  const content = $('#dialog-content');
  content.replaceChildren();
  content.append(makeElement('p', 'eyebrow', room.building.toUpperCase()));
  content.append(makeElement('h2', 'dialog-title', room.name));
  content.append(makeElement('span', `status-pill ${room.available ? 'status-available' : 'status-unavailable'}`, room.available ? 'Available in sample data' : 'Unavailable in sample data'));
  content.append(makeElement('p', 'dialog-description', room.description || 'No description provided.'));
  const details = makeElement('dl', 'details-list');
  const values = [
    ['Room ID', room.id], ['Building', room.building], ['Floor', room.floor ?? 'Not specified'],
    ['Seating capacity', `${room.capacity} ${room.capacity === 1 ? 'seat' : 'seats'}`],
    ['Facilities', room.facilities.length ? room.facilities.join(', ') : 'None listed']
  ];
  values.forEach(([label, value]) => {
    details.append(makeElement('dt', '', label));
    details.append(makeElement('dd', '', String(value)));
  });
  content.append(details);
  const editButton = makeElement('button', 'button button-primary dialog-edit', 'Edit classroom');
  editButton.type = 'button';
  editButton.addEventListener('click', () => { dialog.close(); startEdit(id); });
  content.append(editButton);
  const deleteButton = makeElement('button', 'button button-danger dialog-delete', 'Delete classroom');
  deleteButton.type = 'button';
  deleteButton.dataset.deleteRoom = room.id;
  content.append(deleteButton);
  if (typeof dialog.showModal === 'function') dialog.showModal();
  else showToast(`${room.name}: ${room.capacity} seats, ${room.available ? 'available' : 'unavailable'}.`);
}

function clearValidation() {
  ['name', 'building', 'floor', 'capacity'].forEach((field) => $(`#${field}-error`).textContent = '');
  $('#form-status').textContent = '';
}

function readForm() {
  clearValidation();
  const name = $('#room-name').value.trim();
  const building = $('#room-building').value.trim();
  const floorRaw = $('#room-floor').value.trim();
  const capacityRaw = $('#room-capacity').value.trim();
  const facilities = $('#room-facilities').value.split(',').map((item) => item.trim()).filter(Boolean).filter((item, index, list) => list.findIndex((other) => other.toLocaleLowerCase() === item.toLocaleLowerCase()) === index);
  const description = $('#room-description').value.trim();
  const available = $('#room-available').value === 'true';
  let valid = true;
  if (!name) { $('#name-error').textContent = 'Enter a room name.'; valid = false; }
  if (!building) { $('#building-error').textContent = 'Enter a building name.'; valid = false; }
  let floor = null;
  if (floorRaw !== '') {
    floor = Number(floorRaw);
    if (!Number.isInteger(floor) || floor < 0 || floor > 100) { $('#floor-error').textContent = 'Floor must be a whole number from 0 to 100.'; valid = false; }
  }
  const capacity = Number(capacityRaw);
  if (!capacityRaw || !Number.isInteger(capacity) || capacity < 1 || capacity > 10000) { $('#capacity-error').textContent = 'Capacity must be a whole number from 1 to 10,000.'; valid = false; }
  return { valid, room: { name, building, floor, capacity, available, facilities, description } };
}

function resetForm() {
  form.reset();
  $('#edit-id').value = '';
  $('#form-title').textContent = 'Add a classroom';
  $('#save-room').textContent = 'Add classroom';
  $('#cancel-edit').hidden = true;
  clearValidation();
}

function startEdit(room) {
  $('#edit-id').value = room.id;
  $('#room-name').value = room.name;
  $('#room-building').value = room.building;
  $('#room-floor').value = room.floor ?? '';
  $('#room-capacity').value = room.capacity;
  $('#room-facilities').value = room.facilities.join(', ');
  $('#room-description').value = room.description;
  $('#room-available').value = String(room.available);
  $('#form-title').textContent = `Edit ${room.name}`;
  $('#save-room').textContent = 'Save changes';
  $('#cancel-edit').hidden = false;
  clearValidation();
  location.hash = 'manage';
  $('#room-name').focus();
}

async function handleFormSubmit(event) {
  event.preventDefault();
  const { valid, room } = readForm();
  if (!valid) {
    $('#form-status').textContent = 'Please correct the highlighted fields.';
    const firstInvalid = form.querySelector('.field-error:not(:empty)');
    if (firstInvalid) firstInvalid.previousElementSibling?.focus();
    return;
  }
  const editId = $('#edit-id').value;
  try {
    if (editId) {
      await api.update(editId, room);
      setStorageStatus(`${room.name} updated.`);
      showToast(`${room.name} updated successfully.`);
    } else {
      const created = await api.create(room);
      setStorageStatus(`${room.name} added.`);
      showToast(`${room.name} added with ID ${created.id}.`);
    }
  } catch (error) {
    $('#form-status').textContent = error.message;
    return;
  }
  resetForm();
  refresh();
}

async function deleteClassroom(id) {
  let room;
  try {
    room = await api.get(id);
  } catch (error) { showToast(error.message, true); refresh(); return; }
  if (!window.confirm(`Delete ${room.name} (${room.id})? This cannot be undone.`)) return;
  try {
    await api.remove(id);   // the C++ array shifts later rooms left to close the gap
  } catch (error) { reportError(error); return; }
  setStorageStatus(`${room.name} deleted.`);
  refresh();
  showToast(`${room.name} deleted.`);
}

// The management list is also reachable from each card's Edit action. Delete is in details for clarity.
$('#hero-search-form').addEventListener('submit', (event) => {
  event.preventDefault();
  $('#directory-search').value = $('#hero-search').value.trim();
  location.hash = 'directory';
  renderRooms();
  $('#directory-search').focus({ preventScroll: true });
  $('#directory').scrollIntoView({ behavior: window.matchMedia('(prefers-reduced-motion: reduce)').matches ? 'auto' : 'smooth' });
});
['directory-search', 'availability-filter', 'building-filter', 'group-size', 'sort-filter'].forEach((id) => {
  $(`#${id}`).addEventListener(id === 'directory-search' || id === 'group-size' ? 'input' : 'change', renderRooms);
});
$('#reset-filters').addEventListener('click', () => {
  $('#directory-search').value = '';
  $('#availability-filter').value = 'all';
  $('#building-filter').value = 'all';
  $('#group-size').value = '';
  $('#sort-filter').value = 'name';
  renderRooms();
  $('#directory-search').focus();
});
$('#empty-reset').addEventListener('click', () => $('#reset-filters').click());
$('#dialog-close').addEventListener('click', () => dialog.close());
dialog.addEventListener('click', (event) => { if (event.target === dialog) dialog.close(); });
$('#classroom-form').addEventListener('submit', handleFormSubmit);
$('#cancel-edit').addEventListener('click', resetForm);
$('#reset-data').addEventListener('click', async () => {
  if (!window.confirm('Reset all classroom changes and restore the original fictional sample data?')) return;
  try {
    await api.reset();
  } catch (error) { reportError(error); return; }
  setStorageStatus('Sample data restored.');
  resetForm();
  $('#directory-search').value = '';
  $('#availability-filter').value = 'all';
  $('#building-filter').value = 'all';
  $('#group-size').value = '';
  $('#sort-filter').value = 'name';
  refresh();
  showToast('Original sample classroom data restored.');
});

// Add a delete action to the details dialog without using user data as HTML.
// This delegated handler keeps the delete action keyboard-accessible inside the native dialog.
$('#dialog-content').addEventListener('click', (event) => {
  if (event.target.matches('[data-delete-room]')) {
    const id = event.target.dataset.deleteRoom;
    dialog.close();
    deleteClassroom(id);
  }
});

// Keep the details function behavior simple; add a delete control after each dialog open via dialog event.
dialog.addEventListener('close', () => { /* Native dialog restores focus to its opener. */ });

// Navigation highlight follows the hash.
function updateNavigation() {
  const section = location.hash.replace('#', '') || 'home';
  document.querySelectorAll('[data-nav]').forEach((link) => {
    const active = link.dataset.nav === section || (section === 'manage' && link.dataset.nav === 'manage');
    link.classList.toggle('active', active);
  });
}
window.addEventListener('hashchange', updateNavigation);


$('#search-queue-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  const rawGroup = $('#queue-group-size').value.trim();
  if (rawGroup && (!Number.isInteger(Number(rawGroup)) || Number(rawGroup) < 1 || Number(rawGroup) > 10000)) {
    $('#queue-form-status').textContent = 'Minimum seats must be a whole number from 1 to 10,000.'; return;
  }
  const criteria = { query: $('#queue-query').value.trim(), availability: $('#queue-availability').value, building: $('#queue-building').value, groupSize: rawGroup, sort: $('#queue-sort').value };
  try {
    const result = await api.enqueueSearch(criteria);
    $('#queue-form-status').textContent = `Search added at queue position ${result.position}.`;
    $('#queue-result-status').textContent = 'Your search is waiting to be processed.';
    $('#queue-results').hidden = true;
    await renderSearchQueue(); showToast(`Search request added at position ${result.position}.`);
  } catch (error) { $('#queue-form-status').textContent = error.message; showToast(error.message, true); }
});
$('#process-next-search').addEventListener('click', async () => {
  try {
    const result = await api.processNextSearch();
    $('#queue-result-status').textContent = `Processed request: ${describeSearchRequest(result.processed)}. Found ${result.matchCount} matching classroom(s). ${result.remaining} request(s) remain in the queue.`;
    renderQueueResults(result.rooms); await renderSearchQueue();
  } catch (error) { $('#queue-result-status').textContent = error.message; showToast(error.message, true); await renderSearchQueue(); }
});
$('#refresh-search-queue').addEventListener('click', renderSearchQueue);

setStorageStatus('Connecting to the C++ server…');
refresh().then(() => setStorageStatus('Connected to the C++ server. Data is stored in data/classrooms.txt.'));
renderSearchQueue();
updateNavigation();
