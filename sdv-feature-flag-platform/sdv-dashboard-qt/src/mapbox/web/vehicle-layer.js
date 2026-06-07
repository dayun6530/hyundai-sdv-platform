/* global mapboxgl, THREE */

/**

 * Mapbox custom layer — white 3D sedan (Three.js primitives) + route traffic.

 */

window.SdvVehicleLayer = (function () {

  let installed = false;

  let lastPose = null;

  let trafficPoses = [];

  let visible = true;

  let trafficVisible = true;

  let mapRef = null;



  function buildCarMesh(bodyColor) {

    const group = new THREE.Group();

    const bodyMat = new THREE.MeshStandardMaterial({

      color: bodyColor || 0xf4f6f8,

      metalness: 0.35,

      roughness: 0.45,

    });

    const glassMat = new THREE.MeshStandardMaterial({

      color: 0x1e293b,

      metalness: 0.8,

      roughness: 0.15,

      transparent: true,

      opacity: 0.75,

    });

    const trimMat = new THREE.MeshStandardMaterial({ color: 0x0f172a, metalness: 0.5, roughness: 0.4 });



    const body = new THREE.Mesh(new THREE.BoxGeometry(1.9, 4.6, 1.35), bodyMat);

    body.position.y = 0.72;

    group.add(body);



    const cabin = new THREE.Mesh(new THREE.BoxGeometry(1.65, 2.2, 1.05), glassMat);

    cabin.position.set(0, 1.35, -0.35);

    group.add(cabin);



    const hood = new THREE.Mesh(new THREE.BoxGeometry(1.7, 1.5, 0.9), bodyMat);

    hood.position.set(0, 0.95, 1.35);

    group.add(hood);



    const wheelGeo = new THREE.CylinderGeometry(0.38, 0.38, 0.22, 16);

    [[-0.95, 1.35], [0.95, 1.35], [-0.95, -1.45], [0.95, -1.45]].forEach(([wx, wz]) => {

      const w = new THREE.Mesh(wheelGeo, trimMat);

      w.rotation.z = Math.PI / 2;

      w.position.set(wx, 0.38, wz);

      group.add(w);

    });



    return group;

  }



  function renderPose(pose, model, map, matrix, renderer, scene, camera) {

    const { lng, lat, bearing } = pose;

    const mc = mapboxgl.MercatorCoordinate.fromLngLat([lng, lat], 0);

    const s = mc.meterInMercatorCoordinateUnits();

    const bearingRad = (bearing * Math.PI) / 180;



    const rotationX = new THREE.Matrix4().makeRotationAxis(new THREE.Vector3(1, 0, 0), Math.PI / 2);

    const rotationZ = new THREE.Matrix4().makeRotationAxis(new THREE.Vector3(0, 0, 1), -bearingRad);



    const m = new THREE.Matrix4().fromArray(matrix);

    const l = new THREE.Matrix4()

      .makeTranslation(mc.x, mc.y, mc.z)

      .scale(s, -s, s)

      .multiply(rotationX)

      .multiply(rotationZ);



    camera.projectionMatrix = m.multiply(l);

    renderer.render(scene, camera);

  }



  function createLayer() {

    return {

      id: 'sdv-vehicle-3d',

      type: 'custom',

      renderingMode: '3d',

      onAdd(map, gl) {

        this.map = map;

        this.camera = new THREE.Camera();

        this.egoScene = new THREE.Scene();

        this.trafficScene = new THREE.Scene();

        this.egoScene.add(new THREE.AmbientLight(0xffffff, 0.9));

        this.trafficScene.add(new THREE.AmbientLight(0xffffff, 0.85));

        const dir = new THREE.DirectionalLight(0xffffff, 0.7);

        dir.position.set(60, 90, 45);

        this.egoScene.add(dir);

        const dirT = new THREE.DirectionalLight(0xffffff, 0.65);

        dirT.position.set(60, 90, 45);

        this.trafficScene.add(dirT);

        this.egoModel = buildCarMesh(0xf4f6f8);

        this.trafficModel = buildCarMesh(0xcbd5e1);

        this.egoScene.add(this.egoModel);

        this.trafficScene.add(this.trafficModel);

        this.renderer = new THREE.WebGLRenderer({

          canvas: map.getCanvas(),

          context: gl,

          antialias: true,

        });

        this.renderer.autoClear = false;

      },

      render(gl, matrix) {

        this.renderer.resetState();

        if (visible && lastPose) {

          renderPose(lastPose, this.egoModel, this.map, matrix, this.renderer, this.egoScene, this.camera);

        }

        if (trafficVisible && trafficPoses.length > 0) {

          trafficPoses.forEach((p) => {

            renderPose(p, this.trafficModel, this.map, matrix, this.renderer, this.trafficScene, this.camera);

          });

        }

        this.map.triggerRepaint();

      },

    };

  }



  return {

    install(map) {

      if (!map || installed || typeof THREE === 'undefined') {

        return;

      }

      mapRef = map;

      map.addLayer(createLayer());

      installed = true;

    },

    updatePose(lng, lat, bearingDeg) {

      lastPose = { lng, lat, bearing: bearingDeg };

      if (installed && mapRef) {

        mapRef.triggerRepaint();

      }

    },

    updateTraffic(poses) {

      trafficPoses = Array.isArray(poses) ? poses : [];

      if (installed && mapRef) {

        mapRef.triggerRepaint();

      }

    },

    setTrafficVisible(on) {

      trafficVisible = !!on;

    },

    hide() {

      lastPose = null;

    },

    setVisible(on) {

      visible = !!on;

    },

  };

})();

