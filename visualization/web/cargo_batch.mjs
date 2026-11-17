const MAX_BATTERY = 100;
const BATTERY_RESERVE = 15;
const neighbors = [{ x: 1, y: 0 }, { x: 0, y: 1 }, { x: -1, y: 0 }, { x: 0, y: -1 }];
const key = point => `${point.x},${point.y}`;

function routeLength(map, start, destination, teamSize) {
  const traversable = point => {
    if (point.x < 0 || point.y < 0 || point.x >= map.width || point.y >= map.height || map.walls.has(key(point))) return false;
    const passage = map.passages.find(item => item.cells.some(cell => cell.x === point.x && cell.y === point.y));
    return !passage || passage.capacity >= teamSize;
  };
  if (!traversable(start) || !traversable(destination)) return null;

  const distances = new Map([[key(start), 0]]);
  const queue = [start];
  for (let index = 0; index < queue.length; index++) {
    const current = queue[index];
    const distance = distances.get(key(current));
    if (current.x === destination.x && current.y === destination.y) return distance;
    for (const step of neighbors) {
      const next = { x: current.x + step.x, y: current.y + step.y };
      const nextKey = key(next);
      if (!distances.has(nextKey) && traversable(next)) {
        distances.set(nextKey, distance + 1);
        queue.push(next);
      }
    }
  }
  return null;
}

export function batteryFeasibleCargoPairs(map, teamSize) {
  return map.bays.flatMap(bay => map.destinations.filter(destination => {
    const delivery = routeLength(map, bay, destination, teamSize);
    if (delivery === null || !map.chargers.length) return false;

    const returnRoutes = map.chargers
      .map(charger => routeLength(map, destination, charger, 1))
      .filter(distance => distance !== null);
    if (!returnRoutes.length) return false;
    const returnToCharger = Math.min(...returnRoutes);

    return map.chargers.some(charger => {
      const toPickup = routeLength(map, charger, bay, 1);
      return toPickup !== null && toPickup + delivery + returnToCharger + BATTERY_RESERVE < MAX_BATTERY;
    });
  }).map(destination => ({ bay: bay.id, destination: destination.id })));
}
